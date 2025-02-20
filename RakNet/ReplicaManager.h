/// \file
/// \brief Contains class ReplicaManager.  This system provides management for your game objects and players to make serialization, scoping, and object creation and destruction easier.
///
/// This file is part of RakNet Copyright 2003 Kevin Jenkins.
///
/// Usage of RakNet is subject to the appropriate license agreement.
/// Creative Commons Licensees are subject to the
/// license found at
/// http://creativecommons.org/licenses/by-nc/2.5/
/// Single application licensees are subject to the license found at
/// http://www.rakkarsoft.com/SingleApplicationLicense.html
/// Custom license users are subject to the terms therein.
/// GPL license users are subject to the GNU General Public
/// License as published by the Free
/// Software Foundation; either version 2 of the License, or (at your
/// option) any later version.

#ifndef __REPLICA_MANAGER_H
#define __REPLICA_MANAGER_H

#include "Export.h"
#include "NetworkTypes.h"
#include "DS_OrderedList.h"
#include "PluginInterface.h"
#include "NetworkIDGenerator.h"
#include "DS_Queue.h"
#include "ReplicaEnums.h"

/// Forward declarations
namespace RakNet
{
	class BitStream;
};
class Replica;

/// ReplicaManager is a system manager for your game objects that performs the following tasks:
/// 1. Tracks all locally registered game objects and players and only performs operations to and for those objects and players
/// 2. Allows you to automatically turn off unneeded local and remote functions for your game objects, thus providing convenience and security against unauthorized sends.
/// 3. Sends notifications of existing game objects to new connections, including a download complete message.
/// 4. Sends notifications of new game objects to existing players.
///  A. Serialize and scoping calls are not sent to objects that were not notified of that object.
///  B. Notification calls can be cancelled on a per-object basis.  Object notification sends are tracked on a per-system per-object basis.
/// 5. Configurable per-system per-object scoping.
///  A. Scoping provides a mechanism to hide and unhide remote objects without destroying the whole object, used when when entities should not be destroyed but are currently not visible to systems.
///  B. Serialize calls are not sent to hidden objects.
///  C. Scoping calls can be cancelled on a per-object basis.  Scope is tracked on a per-system per-object basis.
/// 6. Replicate, SetScope, SignalSerializeNeeded, and the corresponding Replica interfaces are processed in RakPeer::Receive, rather than immediately.
///  A. This allows the ReplicaManager to reorganize function calls in order by dependency.  This allows out of order calls, per-object call cancellation (which also cancels dependencies), and per-object call delays (which also delays dependencies)
///  B. For example, although SetScope and SignalSerializeNeeded have a dependency on SetNetworkID(), you can still call them in the constructor and call SetNetworkID() later, as long as it happens before calling RakPeer::Receive()
/// 7. The system is fast, uses little memory, and is intentionally hands off such that it can work with any game architecture and network topology
///
/// What the ReplicaManager system does NOT do for you
/// 1. Actually create or destroy your game objects
/// 2. Associate object destruction events with remote system disconnects.
/// 3. Set networkIDs via SetNetworkID() on newly created objects.
/// 4. Object sub-serialization.  Serialize only granular on the level of entire objects.  If you want to serialize part of the object, you need to set your own flags and indicate in the BitStream which parts were sent and which not.
/// \brief A management system for your game objects and players to make serialization, scoping, and object creation and destruction easier.
class RAK_DLL_EXPORT ReplicaManager : public PluginInterface
{
public:
	/// Constructor
	ReplicaManager();

	/// Destructor	
	~ReplicaManager();

	/// Do or don't automatically call AddParticipant when new systems connect to us.
	/// Won't add automatically add connections that already exist before this was called
	/// Defaults to false
	/// \param[in] autoAdd True or false, to add or not
	void SetAutoParticipateNewConnections(bool autoAdd);

	/// Adds a participant to the ReplicaManager system.  Only these participants get packets and we only accept ReplicaManager packets from these participants.
	/// This way you can have connections that have nothing to do with your game - for example remote console logins
	/// \param[in] playerId Which player you are referring to
	void AddParticipant(PlayerID playerId);

	/// Removes a participant from the data replicator system
	/// This is called automatically on ID_DISCONNECTION_NOTIFICATION and ID_CONNECTION_LOST messages, as well as CloseConnection() calls.
	/// \param[in] playerId Which player you are referring to
	void RemoveParticipant(PlayerID playerId);

	/// Construct the specified object on the specified system
	/// For the object you pass, Replica::SendConstruction will be called on the next update cycle for the player you specify
	/// Nothing is actually created - this just signals that another system wants you to do so.
	/// The other system will get Replica::ReceiveConstruction
	/// If your system assigns NetworkIDs, do so before calling Replicate as the NetworkID is automatically included in the packet.
	/// Replicate packets that are sent to systems that already have this NetworkID are ignored.
	/// \note Objects which are replicated get exactly one call to SendConstruction for every player / object permutation.
	/// \param[in] replica A pointer to your object
	/// \param[in] playerId The participant to send the command to, or the one to exclude if broadcast is true.
	/// \param[in] broadcast True to send to all.  If playerId!=UNASSIGNED_PLAYER_ID then this means send to all but that participant
	void Construct(Replica *replica, PlayerID playerId, bool broadcast);

	/// Call this with your game objects to have them send Replica::SendDestruction.
	/// This will be sent immediately to all participants that have this object.  Those participants will get Replica::ReceiveDestruction
	/// All pending calls for this object, for this player, are canceled.
	/// Nothing is actually deleted - this just signals that the other system called this function.  It is up to you to actually delete your object.
	/// \pre Call Replicate with this object first.
	/// \pre For the other system to get the network message, SetNetworkID on that object must have been called with the same value as GetNetworkID for this object.
	/// \note Call Destruct before DereferencePointer if you plan on calling both, since Destruct will fail with no pointer reference.
	/// \note Calling Destruct with playerId==UNASSIGNED_PLAYER_ID and broadcast true is equivalent to calling DereferencePointer except that Destruct also sends the destruct packet.
	/// \note It is important to call this before deleting your object.  Otherwise this system will crash the next OnUpdate call.
	/// \param[in] replica A pointer to your object
	/// \param[in] playerId The participant to send the command to, or the one to exclude if broadcast is true.
	/// \param[in] broadcast True to send to all.  If playerId!=UNASSIGNED_PLAYER_ID then this means send to all but that participant
	void Destruct(Replica *replica, PlayerID playerId, bool broadcast);

	/// This makes sure the object is tracked, so you can get calls on it.
	/// This will automatically happen if you call Construct, SetScope, or SignalSerializeNeeded with \a replica
	/// Otherwise you need to call this, or for security the system will ignore calls that reference this object, even if given a valid NetworkID
	/// Duplicate calls are safe and are simply ignored.
	/// Best place to put this is in the SetReceiveConstructionCB callback so that all your objects are registered.
	/// \param[in] replica A pointer to your object
	void ReferencePointer(Replica *replica);

	/// Call this before you delete \a replica.  This locally removes all references to this pointer.
	/// No messages are sent.
	/// Best place to put this is in the destructor of \a replica
	/// \param[in] replica A pointer to your object
	void DereferencePointer(Replica *replica);

	/// Sets the scope of your object in relation to another participant.
	/// Objects that are in-scope for that participant will send out Replica::Serialize calls.  Otherwise Serialize calls are not sent.
	/// Scoping is useful when you want to disable sends to an object temporarily, without deleting that object.
	/// Calling this results in Replica::SendScopeChange being called on the local object and Replica::ReceiveScopeChange on the remote object if that object has been created on that remote system.
	/// Your game should ensure that objects not in scope are hidden, but not deallocated, on the remote system.
	/// Replica::SendScopeChange with \a inScope as true will automatically perform Replica::Serialize
	/// \pre Call Replicate with this object first.
	/// \pre For the other system to get the network message, that object must have an NetworkID (set by SetNetworkID()) the same as our object's NetworkID (returned from GetNetworkID()).
	/// \note You can set the default scope with SetDefaultScope()
	/// \note Individual objects can refuse to perform the SendScopeChange call by not writing to the output bitstream while returning true.
	/// \param[in] replica An object previously registered with Replicate
	/// \param[in] inScope in scope or not.
	/// \param[in] playerId The participant to send the command to, or the one to exclude if broadcast is true.
	/// \param[in] broadcast True to send to all.  If playerId!=UNASSIGNED_PLAYER_ID then this means send to all but that participant
	void SetScope(Replica *replica, bool inScope, PlayerID playerId, bool broadcast);

	/// Signal that data has changed and we need to call Serialize() on the \a replica object.
	/// This will happen if the object has been registered, Replica::SendConstruction wrote to outBitStream and returned true, and the object is in scope for this player.
	/// \pre Call Replicate with this object first.
	/// \pre For the other system to get the network message, that object must have an NetworkID (set by SetNetworkID()) the same as our object's NetworkID (returned from GetNetworkID()).
	/// \param[in] replica An object previously registered with Replicate
	/// \param[in] playerId The participant to send the command to, or the one to exclude if broadcast is true.
	/// \param[in] broadcast True to send to all.  If playerId!=UNASSIGNED_PLAYER_ID then this means send to all but that participant
	void SignalSerializeNeeded(Replica *replica, PlayerID playerId, bool broadcast);

	/// Required callback
	/// Set your callback to parse requests to create new objects.  Specifically, when Replica::SendConstruction is called and the networkID of the object is either unset or can't be found, this callback will get that call.
	/// How do you know what object to create?  It's up to you, but I suggest in Replica::SendConstruction you encode the class name.  The best way to do this is with the StringTable class.
	/// \note If you return true from IsNetworkIDAuthority, which you should do for a server or peer, I recommend also encoding the value returned by GetNetworkID() within Replica::SendConstruction into that bitstream and reading it here.  Then set that value in a call to SetNetworkID.  Dereplicate, SetScope, and SignalSerializeNeeded all rely on being able to call GET_OBJECT_FROM_ID which requires that SetNetworkID be called on that object.
	/// \param[in] constructionCB The callback function pointer
	/// \param[in] inBitStream The bitstream that was written to in Replica::SendConstruction
	/// \param[in] timestamp If in Replica::SendConstruction you set sendTimestamp to true, this is the time the packet was sent.  Otherwise it is 0.
	/// \param[in] networkID If the remote object had an NetworkID set by the time Replica::SendConstruction was called it is here.
	/// \param[in] senderId Which PlayerID sent this packet.
	/// \return See ReplicaReturnResult
	void SetReceiveConstructionCB(ReplicaReturnResult (* constructionCB)(RakNet::BitStream *inBitStream, RakNetTime timestamp, NetworkID networkID, PlayerID senderId, ReplicaManager *caller));

	/// Optional callback
	/// Set your callbacks to be called when, after connecting to another system, you get all objects that system is going to send to you when it is done with the first iteration through the object list.
	/// \param[in] sendDownloadCompleteCB The callback function pointer or 0, to call when we send a download complete packet.  Used to append data to the download complete packet - you don't have to set this. The download complete message will still be sent.
	/// \param[out] outBitStream Write whatever you want to this bitstream.  It will arrive in the receiveDownloadCompleteCB callback.
	/// \param[in] currentTime The current time that would be returned by RakNet::GetTime().  That's a slow call I do already, so you can use the parameter instead of having to call it yourself.
	/// \param[in] senderId Who we are sending to
	/// \param[in] receiveDownloadCompleteCB The callback function pointer or 0, to call when we get a download complete packet.  You do need to set this if you want to be notified of the download complete packet.
    /// \param[in] inBitStream The bitstream that was written to in the sendDownloadCompleteCB callback
	/// \return See ReplicaReturnResult
	void SetDownloadCompleteCB(ReplicaReturnResult (* sendDownloadCompleteCB)(RakNet::BitStream *outBitStream, RakNetTime currentTime, PlayerID senderId, ReplicaManager *caller), ReplicaReturnResult (* receiveDownloadCompleteCB)(RakNet::BitStream *inBitStream, PlayerID senderId, ReplicaManager *caller));

	/// This channel will be used for all RakPeer::Send calls
	/// \param[in] channel The channel to use for internal RakPeer::Send calls from this system.  Defaults to 0.
	void SetSendChannel(unsigned char channel);

	/// This means automatically construct all known objects to all new participants
	/// Has no effect on existing participants
	/// Useful if your architecture always has all objects constructed on all systems all the time anyway, or if you want them to normally start constructed
	/// Defaults to false.
	/// \param[in] autoConstruct true or false, as desired.
	void SetAutoConstructToNewParticipants(bool autoConstruct);

	/// Set the default scope for new objects to all players.  Defaults to false, which means Serialize will not be called for new objects automatically.
	/// If you set this to true, then new players will get existing objects, and new objects will be sent to existing players
	/// This only applies to players that connect and objects that are replicated after this call.  Existing object scopes are not affected.
	/// Useful to set to true if you don't use scope, or if all objects normally start in scope
	/// \param[in] scope The default scope to use.
	void SetDefaultScope(bool scope);

	/// Lets you enable calling any or all of the interface functions in an instance of Replica
	/// This setting is the same for all participants for this object, so if you want per-participant permissions you will need to handle that inside your implementation
	/// All functions enabled by default.
	/// \param[in] replica The object you are referring to
	/// \param[in] interfaceFlags A bitwise-OR of REPLICA_SEND_CONSTRUCTION ... REPLICA_SET_ALL corresponding to the function of the same name
	void EnableReplicaInterfaces(Replica *replica, unsigned char interfaceFlags);

	/// Lets you disable calling any or all of the interface functions in an instance of Replica
	/// This setting is the same for all participants for this object, so if you want per-participant permissions you will need to handle that inside your implementation
	/// All functions enabled by default.
	/// \note Disabling functions is very useful for security.
	/// \note For example, on the server you may wish to disable all receive functions so clients cannot change server objects.		
	/// \param[in] replica The object you are referring to
	/// \param[in] interfaceFlags A bitwise-OR of REPLICA_SEND_CONSTRUCTION ... REPLICA_SET_ALL corresponding to the function of the same name
	void DisableReplicaInterfaces(Replica *replica, unsigned char interfaceFlags);

	/// Tells us if a particular system got a SendConstruction() message from this object.  e.g. does this object exist on this remote system?
	/// This is set by the user when calling Replicate and sending (any) data to outBitStream in Replica::SendConstruction
	/// \param[in] replica The object we are checking
	/// \param[in] playerId The system we are checking
	bool IsConstructed(Replica *replica, PlayerID playerId);

	/// Tells us if a particular object is in scope for a particular system
	/// This is set by the user when calling SetScope and sending (any) data to outBitstream in Replica::SendScopeChange
	/// \param[in] replica The object we are checking
	/// \param[in] playerId The system we are checking
	bool IsInScope(Replica *replica, PlayerID playerId);

	/// Returns how many Replica instances are registered.
	/// This number goes up with each non-duplicate call to Replicate and down with each non-duplicate call to Dereplicate
	/// Used for GetReplicaAtIndex if you want to perform some object on all registered Replica objects.
	/// \return How many replica objects are in the list of replica objects
	unsigned GetReplicaCount(void) const;

	/// Returns a previously registered Replica *, from index 0 to GetReplicaCount()-1.
	/// The order that Replica * objects are returned in is arbitrary (it currently happens to be ordered by pointer address).
	/// Calling Dereplicate immediately deletes the Replica * passed to it, so if you call Dereplicate while using this function
	/// the array will be shifted over and the current index will now reference the next object in the array, if there was one.
	/// \param[in] index An index, from 0 to GetReplicaCount()-1.
	/// \return A Replica * previously passed to Construct()
    Replica *GetReplicaAtIndex(unsigned index);

	enum
	{
		REPLICA_EXPLICIT_CONSTRUCTION=1<<0,
		REPLICA_IMPLICIT_CONSTRUCTION=1<<1, // Overridden by REPLICA_EXPLICIT_CONSTRUCTION.  IMPLICIT assumes the object exists on the remote system.
		REPLICA_SCOPE_TRUE=1<<2, // Mutually exclusive REPLICA_SCOPE_FALSE
		REPLICA_SCOPE_FALSE=1<<3, // Mutually exclusive REPLICA_SCOPE_TRUE
		REPLICA_SERIALIZE=1<<4,
	};

	/// \internal
	/// One pointer and a command to act on that pointer
	struct CommandStruct
	{
		Replica *replica; // Pointer to an external object - not allocated here.
		unsigned char command; // This is one of the enums immediately above.
	};

	struct RegisteredReplica
	{
		Replica *replica; // Pointer to an external object - not allocated here.
		RakNetTime lastDeserializeTrue; //   For replicatedObjects it's the last time deserialize returned true.
		unsigned char allowedInterfaces; // Replica interface flags
	};

	struct RemoteObject
	{
		Replica *replica; // Pointer to an external object - not allocated here.
        bool inScope; // Is replica in scope or not?
		RakNetTime lastSendTime;
	};

	struct ReceivedCommand
	{
		PlayerID playerId;
		NetworkID networkID;
		unsigned command; // A packetID
		unsigned u1;
		RakNet::BitStream *userData;
	};


	static int RegisteredReplicaComp( Replica* const &key, const ReplicaManager::RegisteredReplica &data );
	static int RemoteObjectComp( Replica* const &key, const ReplicaManager::RemoteObject &data );
	static int CommandStructComp( Replica* const &key, const ReplicaManager::CommandStruct &data );
	
	/// \internal
	/// One remote system
	struct ParticipantStruct
	{
		~ParticipantStruct();

		// The player this participant struct represents.
		PlayerID playerId;

		// Call sendDownloadCompleteCB when REPLICA_SEND_CONSTRUCTION is done for all objects in commandList
		// This variable tracks if we did it yet or not.
		bool callDownloadCompleteCB;

		// Sorted list of Replica*, sorted by pointer, along with a command to perform on that pointer.
		// Ordering is just for fast lookup.
		// Nothing is allocated inside this list
		DataStructures::OrderedList<Replica *, CommandStruct, ReplicaManager::CommandStructComp> commandList;

		// Sorted list of Replica*, sorted by pointer, along with if that object is inScope or not for this system
		// Only objects that exist on the remote system are in this list, so not all objects are necessarily in this list
		DataStructures::OrderedList<Replica *, RemoteObject, ReplicaManager::RemoteObjectComp> remoteObjectList;

		// List of pending ReceivedCommand to process
		DataStructures::Queue<ReceivedCommand*> pendingCommands;
	};

	static int ParticipantStructComp( const PlayerID &key, ReplicaManager::ParticipantStruct * const &data );

protected:
	/// Frees all memory
	void Clear(void);
	// Processes a struct representing a received command
	ReplicaReturnResult ProcessReceivedCommand(ParticipantStruct *participantStruct, ReceivedCommand *receivedCommand);
	// Used to construct an object, the same as the user function call but implicitly.  Implicit means that
	// The object is assumed to exist on the remote system, rather than asking the user via the callback.
	void Construct(Replica *replica, PlayerID playerId, bool broadcast, bool implicitConstruction);

	// Plugin interface functions
	void OnUpdate(RakPeerInterface *peer);
	void OnAttach(RakPeerInterface *peer);
	bool OnReceive(RakPeerInterface *peer, Packet *packet);
	void OnCloseConnection(RakPeerInterface *peer, PlayerID playerId);
	void OnDisconnect(RakPeerInterface *peer);

	/// List of objects replicated in the Replicate function.
	/// Used to make sure queued actions happen on valid pointers, since objects are removed from the list in Dereplicate
	/// Sorted by raw pointer address using the default sort
	DataStructures::OrderedList<Replica *, RegisteredReplica, ReplicaManager::RegisteredReplicaComp> replicatedObjects;

	/// List of participants 
	/// Each participant has several queues of pending commands
	/// Sorted by playerID
	/// The only complexity is that each participant also needs a list of objects that mirror the variable replicatedObjects so we know per-player if that object is in scope
	DataStructures::OrderedList<PlayerID, ParticipantStruct *, ReplicaManager::ParticipantStructComp> participantList;

	// Internal functions
	ParticipantStruct* GetParticipantByPlayerID(const PlayerID playerId) const;

	// Callback pointers.

	// Required callback to handle construction calls
	ReplicaReturnResult (* _constructionCB)(RakNet::BitStream *inBitStream, RakNetTime timestamp, NetworkID networkID, PlayerID senderId, ReplicaManager *caller);

	// Optional callbacks to send and receive download complete.
	ReplicaReturnResult (* _sendDownloadCompleteCB)(RakNet::BitStream *outBitStream, RakNetTime currentTime, PlayerID senderId, ReplicaManager *caller);
	ReplicaReturnResult (* _receiveDownloadCompleteCB)(RakNet::BitStream *inBitStream, PlayerID senderId, ReplicaManager *caller);

	// Channel to do send calls on.  All calls are reliable ordered except for Replica::Serialize
	unsigned char sendChannel;

	// Stores what you pass to SetAutoParticipateNewConnections
	bool autoParticipateNewConnections;

	bool defaultScope;
	bool autoConstructToNewParticipants;

	RakPeerInterface *rakPeer;
};


#endif
