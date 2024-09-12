/**
 * @file DynBuffer.h
 * @brief This header file contains the class definitions to create a dynamic buffer, which includes the class
 * declaration, member functions, and member variables.
 *
 * @author Rakesh Kumar
 */

#pragma once

#ifndef DYNAMIC_BUFFER_H
#define DYNAMIC_BUFFER_H

#include <iostream>					// For std::cout
#include <sstream>					// For std::ostringstream
#include <functional>				// For lambda
#include <set>						// For std::set
#include <list>						// For std::list
#include <string>					// For std::string
#include <stdlib.h>					// For malloc
#include <stdexcept>				// For exceptions
#include <utility>					// For std::forward
#include <type_traits>				// For matching template parameters
#include <atomic>					// For std::atomic
#include <thread>					// For std::thread, for owner's thread
#include <mutex>					// For mutex locks on BufferSegment write operations
#include <condition_variable>		// For working with conditional variables
#include <utility>					// For std::pair<T,V> and std::move
#include <vector>					// For std::vector
#include <tuple>					// For std::tuple for direct hooks to the buffer
#include <stdexcept>				// For std::exception, std::runtime_error, throwing and handling exceptions

const int INVALID_ID = 0;

using ull = unsigned long long;

void debug(std::string msg) {
	std::cout << msg << std::endl;
}

/**
 * @brief `BUFFER_SEGMENT_ACCESS_LEVEL` enum defines the access level of an owner (instance of `BufferSegmentOwner`) on a
 * buffer segment (`BufferSegment`)
 *
 * Here is what every constant defined in this enum means:
 * INVALID		- The owner has not been assigned an access level yet or it is sleeping.
 * READ			- The owner has read only access on the buffer segment.
 * WRITE		- The owner has write only access on the buffer segment.
 * READ_WRITE	- The owner has both read and write access on the buffer segment.
 */
enum BUFFER_SEGMENT_ACCESS_LEVEL {
	INVALID,						// INVALID (NO ACCESS, SLEEPING)
	READ,							// READ ONLY
	WRITE,							// WRITE ONLY
	READ_WRITE						// READ & WRITE
};

/**
* @brief A class representing the owner of a buffer segment (`BufferSegment` instance)
*
* Note that the UID of the owner is decided only when it is used in a buffer. If the owner already has an ID then it means
* that this owner is being used somewhere in some dynamic buffer and is not allowed to be used in any other
* buffer.
*
* The destruction of an instance of `BufferSegmentOwner` class is managed by `DynBuffer` class's destructor. The destructor
* cannot be called explicitly.
*/
class BufferSegmentOwner {

public:

	// Delete copy constructor
	BufferSegmentOwner(const BufferSegmentOwner&) = delete;
	// Delete assignment operator
	BufferSegmentOwner& operator=(const BufferSegmentOwner&) = delete;

	template <typename T> friend class DynBuffer;
	template <typename T> friend class BufferSegment;

	/**
	* @brief Constructor to create an anonymous owner with provided access level on the buffer segment.
	*
	* @param accessLevel The access level with which the buffer segment will be used.
	*/
	BufferSegmentOwner(enum BUFFER_SEGMENT_ACCESS_LEVEL accessLevel) : bufferSegmentAccessLevel(accessLevel) {}

	/**
	* @brief Constructor to create an owner with the given name with provided access level on the buffer segment.
	*
	* @param name The name of the owner.
	* @param accessLevel The access level with which the buffer segment will be used.
	*/
	BufferSegmentOwner(
		std::string name, BUFFER_SEGMENT_ACCESS_LEVEL accessLevel
	) : name(name), bufferSegmentAccessLevel(accessLevel) {}

	static std::pair<BufferSegmentOwner*, BufferSegmentOwner*> getReaderWriterPair(std::string readerName, std::string writerName) {
		BufferSegmentOwner* reader = new BufferSegmentOwner(readerName, BUFFER_SEGMENT_ACCESS_LEVEL::READ);
		BufferSegmentOwner* writer = new BufferSegmentOwner(writerName, BUFFER_SEGMENT_ACCESS_LEVEL::WRITE);

		reader->isPartOfReaderWriterPair = true;
		writer->isPartOfReaderWriterPair = true;

		reader->partner = writer;
		writer->partner = reader;

		return std::pair<BufferSegmentOwner*, BufferSegmentOwner*>(reader, writer);
	}

	/**
	* @brief Accessor function to get the ID of this owner
	*/
	ull getID() const {
		return UID;
	}

	/**
	* @brief Accessor function to get the access level of the owner on the buffer segment(s) it owns.
	*/
	BUFFER_SEGMENT_ACCESS_LEVEL getAccessLevel() const {
		return bufferSegmentAccessLevel;
	}

private:

	std::string name{};						// The name of the owner (default : empty indicating no name)
	ull UID{ INVALID_ID };					// The unique ID of the owner (initially 0). Every UID that has the
	// value 0 means that the UID has not been set.
	std::thread* ownerThread{ nullptr };		// Owner's thread (uninitialized), initialize using `new` keyword
	std::mutex* ownerThreadMutex{ new std::mutex };            // Used to lock on to owner thread before changing it

	BUFFER_SEGMENT_ACCESS_LEVEL bufferSegmentAccessLevel{ BUFFER_SEGMENT_ACCESS_LEVEL::INVALID };

	int refCount{ 0 };							// The number of references to this particular instance (default 0).
	// When this instance of `BufferSegmentOwner` is used anywhere, the
	// refCount has to be incremented and when erased from a
	// `BufferSegment`'s owner's list, `refCount` has to be decremented.

	BufferSegmentOwner* partner{ nullptr };
	bool isPartOfReaderWriterPair{ false };

	/*
	* The index at which this buffer segment owner is reading the buffer. Note that writing index is not present in
	* this buffer segment for the very reason that multiple arbitrary reads are allowed on a buffer segment while
	* only one owner is allowed to perform a complete non-arbitrary write to the entire buffer segment. The entire
	* buffer segment might not be used totally so the lock for write will be removed and until the partner owner
	* (if present) reads the entire buffer segment already written to or reads it arbitrarly till complete read, the
	* permission for write will not be given until the entire buffer segment has been read. This buffer segment
	* reading index is reset as soon as last element is read by this owner.
	*/
	std::atomic<unsigned long long> bufferSegmentItemsArrayReadIndex{ 0ULL };

	/*
	* The index of the buffer segment being read. The owner advances linearly to the right of the
	* std::vector<BufferSegment<T>*> as reading is done.
	*
	* TODO: Use std::vector instead of a list in storing buffer segments. This will allow to use a specific buffer
	* segment and will be good for pruner threads as well.
	*/
	std::atomic<unsigned long long> bufferSegmentReadIndex{ 0ULL };

	/**
	* @brief Destructor
	*/
	~BufferSegmentOwner() {
		if (ownerThread != nullptr) {
			// Wait for owner's thread to finish its task before deleting it
			if (ownerThread->joinable()) {
				ownerThread->join();
			}
			delete ownerThread; // Delete the ownerThread object
			ownerThread = nullptr;
		}
		// delete the mutex
		delete ownerThreadMutex;
		ownerThreadMutex = nullptr;
	}

	/**
	* @brief Assigns unique ID to this owner
	*
	* @param dynamicBuffer Pointer to an instance of `DynBuffer` class which calls this method.
	*
	* NOTE: This function is meant to be called by an instance of `DynBuffer` class only.
	* The function looks for IDs of owners already present in the various segment buffers and generates a unique ID
	* for this owner.
	*
	*/
	void assignUID() {
		static ull uniqueId {0};
		try {
			if(uniqueId != ULLONG_MAX)
				UID = ++uniqueId;
			else {
				throw std::runtime_error("Number of unique IDs exhausted.");
			}
		}
		catch (const std::exception& e) {
			// UID Generation failed
			UID = INVALID_ID;
			std::cout << e.what() << std::endl; // or log
		}
	}

	/**
	* @brief Checks if this owner has an ID
	*
	* @return true if this owner has an ID, false otherwise
	*/
	bool hasUID() const {
		return UID != INVALID_ID;
	}

	/**
	* @brief Get a pointer-to-pointer to this owner's thread.
	*/
	std::thread** getPPThread() {
		return &ownerThread;
	}

	/**
	* @brief Get the reference count.
	*
	* Number of references to this instance.
	*/
	int getRefCount() const {
		return refCount;
	}

	/**
	* @brief Increments count of references
	*/
	void incrementRefCount() {
		++refCount;
	}

	/**
	* @brief Increments count of references
	*/
	void decrementRefCount() {
		++refCount;
	}
};

/**
* @brief A buffer segment
*
* Creation of buffer segment is in the hands of `DynBuffer` instance.
* A buffer segment allows multiple reads but a single complete write at a time.
*
* A buffer segment can have multiple readers so even if the reader associated with the writer have already read the written
* contents, the reader will not remove itself from the list of owners of that buffer segment. It is the work of pruner
* threads to delete buffer segments which in turn clears the owners as well.
*/
template <typename T> class BufferSegment {
public:

	template <typename D> friend class DynBuffer;

	// Delete copy constructor
	BufferSegment(const BufferSegment&) = delete;
	// Delete assignment operator
	BufferSegment& operator=(const BufferSegment&) = delete;

	// Delete default constructor
	BufferSegment() = delete;

private:

	std::atomic<T*> items{ nullptr };								// This array houses items within a buffer segment. This array can be used
	// in reading a large file which reads in chunk. Reading in large chunks
	// would be fast as the Dynamic Buffer (DynBuffer) that houses Buffer
	// Segments (BufferSegment instances) will grow/shrink dynamically as per
	// the read speed with a maximum overall memory limit of some X MB.
	unsigned long long size{ 0 };										// The size of the `items` array.
	std::atomic<unsigned long long> writingIndex{ 0 };				// The index before which other owners have access to perform read
	// operations. Also it is the index that is used to write to the `items`
	// array.

	std::list<BufferSegmentOwner*>*
		owners{ nullptr };								// A set of owners of this buffer segment
	BufferSegmentOwner* currentOwner{ nullptr };		// The current owner of this buffer segment

	std::mutex* writerMutex{ new std::mutex };								// A mutex for using lock on write operations
	std::mutex* readerMutex{ new std::mutex };								// A mutex for using lock on read operations

	/*
	* Note that there is no special bool variable for those owners who have both read and write access. Instead, the
	* following two variables -- `inWrite` and `inRead` are used.
	*/
	/**
	* A flag variable that marks whether this buffer segment is in use and is being written to. If a thread has a
	* lock on this buffer segment and is writing to the buffer, other threads will not be allowed to write to the same.
	*/
	std::atomic<bool> inWrite{ false };

	/**
	* A flag variable that marks whether this buffer segment is in use and is being read. The thread simply gets an
	* access to the buffer segment without acquiring a lock on the critical section / resource. If data is being
	* written by another thread then until that thread relinquishes its control, reading is not allowed.
	*/
	std::atomic<bool> inRead{ false };

	/**
	* @brief Destructor
	*
	* Calls to the destructor destroys the buffer items contained within this `BufferSegment` after and before
	* performing necessary cleanup.
	*/
	~BufferSegment() {
		// wait for owners to finish their task on this buffer segment
		for (typename std::list<BufferSegmentOwner*>::iterator it = owners->begin(); it != (owners->end());) {
			BufferSegmentOwner* pOwner = *it;
			// Check if reference count of the owner instnace will drop to 0 after deletion
			if (pOwner->getRefCount() == 1) {
				// delete this owner
				delete* it;
				*it = nullptr;
			}
			else {
				// The owner must not be deleted here
				// Wait for owner's Thread to finish its execution (if there are any thread associated)
				std::thread** pOwnerThread = pOwner->getPPThread();
				if (pOwnerThread != nullptr && ((*pOwnerThread) != nullptr) && (*pOwnerThread)->joinable()) {
					(*pOwnerThread)->join();
				}
				// Decrease the owner's reference count by 1
				pOwner->decrementRefCount();
			}
			it = owners->erase(it);
		}
		// Remove currentOwner
		currentOwner = nullptr;
		// free space occupied by items array
		if (items != nullptr) {
			delete items;
			items = nullptr;
		}
		// remove mutexes
		delete writerMutex;
		writerMutex = nullptr;
		delete readerMutex;
		readerMutex = nullptr;
	}

	/**
	* @brief Constructor to initialize a buffer segment of size `size` and initialize the owners set
	*/
	BufferSegment(
		unsigned long long size
	) : items((T*)malloc(sizeof(T)* size)),
		size(size), writingIndex(0),
		currentOwner(nullptr),
		inWrite(false),
		inRead(false),
		owners(new std::list<BufferSegmentOwner*>) {}

	/**
	* @brief Constructor to initialize a buffer segment of size `size` with its owner `pOwner`
	*
	* @param size The size of the buffer segment.
	* @param pOwner Pointer to the owner (a `BufferSegmentOwner`)
	*/
	BufferSegment(
		unsigned long long size, BufferSegmentOwner* pOwner
	) : items((T*)malloc(sizeof(T)* size)),
		size(size),
		writingIndex(0),
		currentOwner(pOwner),
		inWrite(false),
		inRead(false),
		owners(new std::list<BufferSegmentOwner*>) {
		owners->push_back(pOwner);
		// increment the reference count
		pOwner->incrementRefCount();
	}

	/**
	* @brief Get the buffer segment size
	*
	* @return Size of this buffer segment.
	*/
	int getBufferSegmentSize() {
		return size;
	}

	/**
	* @brief Assigns a new owner to this buffer segment
	*
	* @param pOwner Pointer to the owner (a `BufferSegmentOwner`)
	*/
	void ownBufferSegment(BufferSegmentOwner* pOwner) {
		// Check if this owner (pOwner) has a valid ID
		if (pOwner->getID() == INVALID_ID) {
			throw std::runtime_error("ERR -- owner rejected -- invalid id");
		}
		// Check if this owner already exists in the set of owners
		if (!(doesOwnerExist(pOwner))) {
			owners->push_back(pOwner);
			// Increase reference count by one for the owner
			pOwner->incrementRefCount();
		}
		if (!(doesOwnerExist(pOwner))) {
			owners->push_back(pOwner);
			// Increment reference count of the owner
			pOwner->incrementRefCount();
		}
		else {
			throw std::runtime_error("ERR -- owner rejected -- owner already present");
		}
	}

	/**
	* @brief Checks if the owner exists for this buffer segment
	*
	* @param pOwner Pointer to the owner (a `BufferSegmentOwner`)
	*/
	bool doesOwnerExist(BufferSegmentOwner* pOwner) {
		if (pOwner != nullptr) {
			auto ownerID = pOwner->getID();
			// Read all owners
			for (auto it = owners->begin(); it != owners->end(); ++it) {
				// Iterate through the set to find an owner with the matching ID
				if (*it != nullptr && ((*it)->getID()) == ownerID) {
					return true;
				}
			}
			return false;
		}
		else {
			throw std::runtime_error("ERR -- pOwner does not exist (nullptr)");
		}
	}

	/**
	 * @brief Checks whether two owners are equal by comparing their IDs.
	 *
	 * @param o1 The first parameter for BufferSegmentOwner.
	 * @param o2 The second parameter for BufferSegmentOwner.
	 * @return true If both the owners match, false otherwise
	 */
	bool doOwnersMatch(BufferSegmentOwner* o1, BufferSegmentOwner* o2) {
		return o1->getID() == o2->getID();
	}

	/**
	* @brief Revokes ownership of this buffer segment frome owner given by parameter `pOwner`
	*
	* Any task being performed by this owner will need to be completed before ownership is revoked.
	*
	* @param pOwner Pointer to the owner (a `BufferSegmentOwner`)
	*/
	void revokeOwnership(BufferSegmentOwner* pOwner) {
		try {
			// Check if the requested owner exists in relation to this buffer segment
			bool doesOwnerExists = doesOwnerExist(pOwner);
			if (doesOwnerExists) {
				// Check the reference count of the owner
				if (pOwner->getRefCount() == 1) {
					// Check if pOwner is the currentOwner
					if (doOwnersMatch(pOwner, currentOwner)) {
						// nullify current owner
						currentOwner = nullptr;
					}
					// Decrease the reference count by 1
					pOwner->decrementRefCount();
					// Remove owner from the set of owners for this buffer segment
					owners->remove(pOwner);
					// Delete the owner (the destructor of `BufferSegmentOwner` handles its thread task completion and
					// deletion, so, no explicit or double deletion has to be done here for that.
					delete pOwner;
				}
				else {
					// There are more buffer segments having this ownership
					// Hence, remove only the owner after all its task is finished on this buffer segment
					std::thread** ppThread = pOwner->getPPThread();
					if (ppThread != nullptr && (*ppThread) != nullptr) {
						if ((*ppThread)->joinable()) {
							(*ppThread)->join();
						}
						// delete thread instance from this buffer segment owner
						delete* ppThread;
						(*ppThread) = nullptr;
					}
					// Decrease the reference count by 1
					pOwner->decrementRefCount();
					// Remove owner from the set of owners for this buffer segment
					owners->remove(pOwner);
				}
			}
		}
		catch (const std::runtime_error& RE) {
			throw RE; // re-throw to the caller
		}
	}

	/**
	* @brief Checks if this buffer segment is in use.
	*
	* @return `true` if this buffer segment is in use else `false`
	*/
	bool isBufferSegmentInUse() {
		return inRead || inWrite;
	}

	/**
	* @brief Check whether this buffer segment is being read.
	*
	* @return true if this buffer segment is being read.
	*/
	bool isReading() const {
		return inRead;
	}

	/**
	* @brief Check whether this buffer segment is being written to.
	*
	* @return true if any piece of data is being written to this buffer segment.
	*/
	bool isWriting() const {
		return inWrite;
	}

	/**
	* @brief Checks if this buffer segment is writable or not.
	*
	* @return true if this buffer segment is writable, false otherwise.
	*/
	bool isWritable() {
		return
			!inWrite								// Places the condition that when an owner is writing to this
			// buffer segment, no other owners are allowed to write to the
			// same.
			||
			(writingIndex == 0);				// Places the condition that if there are contents already
		// written to this buffer segment and the reader associated with
		// the writer has not finished/started reading. The reader
		// associated with the writer is responsible to clear the buffer
		// segment `items` array and reset the `writingIndex` to zero(0).
		// The associated reader will read quickly, detach, return contents
		// and wait for other reader owners to finish reading the contents
		// of this buffer segment and then the associated reader thread will
		// join.
	}
};

/**
* @class DynBuffer
* @brief Used to create and manage a dynamic buffer
*
* This class employs a single thread which overlooks entire tasks inside it. For reference use the name "DynBufferThread".
*
* Use this class as the interface for detailed level operations on the buffer segment. This class holds more than one
* instance of the `BufferSegment` class in a list of `BufferSegment**`.
*
* A dynamic buffer stores several buffer segments in a linear fashion. Some of the threads might be reading a buffer
* segment values, some might try to write to it. So in this list of buffer segments, some owners might still be accessing
* the buffer segment, some might have finished working with the buffer and optionally moves on to the next buffer segment
* (watching out if they have their ownership in the next buffer segment).
* Thus the incoming or outgoing data through the Dynamic Buffer will be stored in a continuous, linear, zero-based
* indexing fashion so as to maintain data consistency.
*
* The actual purpose of having a `DynBuffer` class is to create a dynamic buffer which will allow to create more efficient
* buffer that employs Pruner thread(s) divided among "regions" to free resources (e.g., `items` dynamic array of
* `BufferSegment` class) and erase the `BufferSegment` instance from the list of `BufferSegment`s.
* A buffer segment of any size can be requested and created any time the owner wants to.
* A "region" for the Pruner threads is defined as the chunk of `BufferSegment` list (`*bufferSegments`) which will be
* looked over by the Pruner thread employed by the `DynBuffer` dynamic buffer to cleanup the `BufferSegment`s and erase
* that `BufferSegment` instance itself from the `*bufferSegments` list. The number of Pruner threads may increase if the
* size of the data being read is too large which will create a requirement for more `BufferSegment`s. So if there are no
* owners to a buffer, it will be cleaned & removed by the Pruner thread.
*
* NOTE 1: A buffer segment not having any active threads reading or writing does not mean that it will not have owners. If
* it has owners, it will still remain in the memory because it is unpredictable whether the owner's thread will be used
* again. Only when the ownership is revoked, and there are zero(0) owners to that buffer segment, the buffer segment is
* dropped.
* NOTE 2: Whenever a new owner arrives with a write permission, a new buffer segment is created and owned by this new
* owner. An owner with a write permission handles writing to the buffer segments it owns single handedly and no extra
* owner is required to write the contents that the original owner was supposed to write.
* NOTE 3: Any number of owners can read from the buffer. There is no restriction on reading a buffer segment except when
* another owner is writing to the buffer segment.
* NOTE 4: If an owner has finished writing to a specific buffer segment, all other waiting owners will read from the buffer
* when write operations are over.
*/
template <typename T> class DynBuffer {

public:

	// Delete copy constructor
	DynBuffer(const DynBuffer&) = delete;
	// Delete assignment operator
	DynBuffer& operator=(const DynBuffer&) = delete;

	/**
	* @brief Destructor
	*
	* NOTE: ORDER OF DELETION AND NON-DANGLING MATTERS THE MOST
	*
	* WARNING: DO NOT DELETE THIS BUFFER ITSELF
	* THE DESTRUCTOR HAS TO BE CALLED ON AN INSTANCE OF THIS CLASS MANUALLY OR MANAGED BY A SMART POINTER
	*/
	~DynBuffer() {
		// Free buffer segments, clear and delete
		for (auto it = bufferSegments->begin(); it != bufferSegments->end();) {
			// Ensure *it is not null before dereferencing
			if (*it != nullptr) {
				delete* it;					// Delete the BufferSegment object
				*it = nullptr;
			}
			it = bufferSegments->erase(it);		// Erase the pointer from the set and move to the next element
		}
		delete bufferSegments;
		bufferSegments = nullptr;
	}

	/**
	* @brief Default constructor to instantiate a dynamic buffer of zero size.
	*/
	DynBuffer() : bufferSegments(new std::list<BufferSegment<T>*>()) {}

	/**
	* @brief Constructor to instantiate a dynamic buffer with given initial size and owner.
	*
	* @param initialSize The size of the buffer segment at the time of instantiation of dynamic buffer
	* (single buffer segment is created)
	* @param pOwner Pointer to the owner of the buffer segment to be assigned to the buffer segment
	*/
	DynBuffer(int initialSize, BufferSegmentOwner* pOwner) :
		bufferSegments(new std::list<BufferSegment<T>*>()) {
		// Assign ID to the owner
		pOwner->assignUID();
		// Add a buffer to start with size of the buffer segment as `initialSize`
		BufferSegment<T>* bufferSeg = new BufferSegment<T>(initialSize, pOwner);
		bufferSegments->push_back(bufferSeg);
	}

	/**
	* @brief Constructor to instantiate a dynamic buffer with give initial size and owner with multiple
	* buffer segments
	*
	* Note that this constructor will be used rarely.
	*
	* @param initialSize The size of the buffer segment at the time of instantiation of dynamic buffer
	* (multiple buffer segements are created)
	* @param pOwner Pointer to the owner of the buffer segment to be assigned to the buffer segment
	* @param counts The number of buffer segments to create
	*/
	DynBuffer(
		int initialSize, BufferSegmentOwner* pOwner, int counts
	) : bufferSegments(new std::list<BufferSegment<T>*>()) {
		// Assign ID to the owner
		pOwner->assignUID();
		// add `counts` number of buffer segments to the buffer segment list (`*bufferSegments`) of size
		// `initialSize` with their owner `pOwner`
		while (counts > 0) {
			BufferSegment<T>* bufferSeg = new BufferSegment<T>(initialSize, pOwner);
			bufferSegments->push_back(bufferSeg);
			--counts;
		}
	}

	/**
	* @brief This function is used for any mixed use/operation on the buffer.
	*
	* The owner is verified before any operation is performed on the buffer.
	* If there exists no owner as such then this owner will have the ownerhip of the buffer.
	*
	* In a write operation or a read and write operations, if, immediately there is a "busy" buffer segment in the
	* succession or when the next buffer has some owners with read permission then the next buffer segment is not
	* touched for write operation or read and write operations.Rather, a buffer segment in the list is added,
	* owner is assined, current owner is set, and a pointer to it is returned immediately for write or read and
	* write operations. Data is written. If the data being written doesn't fit in, a new buffer segment is created
	* of the same size and the process is repeated.
	*
	* @param pOwner Pointer to the owner of the buffer segment in use
	* @param func The function to execute with the buffer.
	* @param lambdaArgs The arguments to pass to the function.
	* @return The result (R) of the function execution.
	*/
	// TODO
	template <
		typename R,                              // Return Type
		typename... Args>                        // Lambda Arguments Types
	R use(BufferSegmentOwner* pOwner, std::function<R(Args...)> func, Args&&... lambdaArgs) {
		try {
			// Iterator for use within this `use` function.
			typename std::list<BufferSegment<T>*>::iterator bufferSegsIterator = bufferSegments->begin();
			// Check if the owner has its ID
			if (!(pOwner->hasUID())) {
				// The owner's ID does not exist. Assign an ID to it.
				pOwner->assignUID();
				// Certainly it did not own a buffer segment, since an owner without an ID can not own a buffer
				// segment.
				// Create new buffer segment and assign the owner.
				BufferSegment<T>* tempBuffSeg = new BufferSegment<T>(1024, pOwner); // 1024 * sizeof(T) TODO:
				// Arrange defaults for various operations: Terminal I/O, Application I/O, File I/O, Networking:
				// Mobile Data, Bluetooth, WiFi
				// Attach this buffer segment in the list of buffer segments (`bufferSegments`)
				bufferSegments->push_back(tempBuffSeg);
				// reset iterator to use the item at the end of the list
				bufferSegsIterator = bufferSegments->end();
			}
			else {
				/*
				* Try to find first buffer segment with this owner, if not found, create a buffer segment, assign this
				* owner and put it in the `bufferSegments` list. Also if no buffer segment exists, create one and
				* assign the owner.
				*/
				for (; bufferSegsIterator != bufferSegments->end(); bufferSegsIterator++) {
					if ((*bufferSegsIterator)->doesOwnerExist(pOwner)) {
						// stop here, now `bufferSegsIterator` iterator will be used further
						break;
					}
				}
				if (bufferSegsIterator == bufferSegments->end()) {
					// No such buffer segment found, create one
					BufferSegment<T>* tempBuffSeg = new BufferSegment<T>(1024, pOwner); // 1024 * sizeof(T) TODO:
					// Arrange defaults for various operations: Terminal I/O, Application I/O, File I/O, Networking:
					// Mobile Data, Bluetooth, WiFi
					// Attach this buffer segment in the list of buffer segments (`bufferSegments`)
					bufferSegments->push_back(tempBuffSeg);
					// reset iterator to use the item at the end of the list
					bufferSegsIterator = bufferSegments->end();
				}
			}
			// Now it is sure that owner exists with its buffer segment
			/*
			* WARNING: DO NOT USE DIRECT LOCKS ON ANY RESOURCE/CRITICAL SECTION WITHIN THIS `use` FUNCTION. THE
			* LOCKING ON RESOURCES ARE TO BE DONE BY OTHER FUNCTIONS AND OVERLOADED OPERATORS SUCH AS WRITE,
			* READ, +, -, << (READ IN), >> (READ OUT), ETC. AND THESE SPECIFIED FUNCTIONS ARE USED ONLY WITHIN THE
			* PROVIDED LAMBDA IN THE ARGUMENTS. USE OF FUNCTIONS ASSOCIATED WITH MODIFICATION OF BUFFER SEGMENT
			* AND FUNCTIONS THAT ALLOW READING OF DATA FROM A BUFFER SEGMENT ARE RESPONSIBLE FOR SETTING THE
			* `INWRITE` AND `INREAD` BOOL VARIABLES BY USE OF LOCK.
			*/

			// Execute the function
			if constexpr (std::is_same_v<R, void>) {
				std::invoke(func, std::forward<Args>(lambdaArgs)...);
			}
			else {
				return std::invoke(func, std::forward<Args>(lambdaArgs)...);
			}
		}
		catch (const std::exception& e) {
			// re-throw to the caller
			throw e;
		}
	}

	/**
	* @brief Starts writing a single item to that buffer segment whose `writingIndex` has not exhausted and the owner
	* of that buffer segment is `*pOwner`.
	*
	* This is a blocking function. It blocks the read operation on the current buffer segment being operated on by
	* the owner.
	*
	* No need to dynamically manage the size of the buffer segment here. If new buffer segment is required, a new
	* buffer segment of required size will be created with the same owner and write access.
	*/
	void write(T item, BufferSegmentOwner* pOwner) {
		// Lock the critical section that exposes operations on owner's threads
		std::lock_guard<std::mutex> lock(*(pOwner->ownerThreadMutex));
		// Check if this owner is pointing to nullptr
		if (pOwner != nullptr && ((pOwner->getID()) != INVALID_ID)) {
			// Check for any previous running thread from this owner
			std::thread** th = pOwner->getPPThread();
			if (th != nullptr) {
				if (*th != nullptr) {
					if ((*th)->joinable()) {								// Check if the thread is joinable
						(*th)->join();										// Wait for the thread to finish its tasks
					}
				}
				delete* th;                                             // Delete the old thread
				*th = nullptr;											// Nullify old thread pointer

				// Assign new thread with new task
				*th = new std::thread([this, item, pOwner]() {
					// check if this owner has right access to write to the buffer
					if (pOwner->getAccessLevel() == BUFFER_SEGMENT_ACCESS_LEVEL::WRITE) {
						// Get the buffer segment with owner as `*pOwner`
						auto bufferSegsOwned = bufferSegmentsOwned(pOwner);
						if (bufferSegsOwned == nullptr || bufferSegsOwned->empty()) {
							// Create one & insert in the list of buffer segment
							BufferSegment<T>* tempBSeg = new BufferSegment<T>(
								1024, // TODO: Aap to jaante hee hain
								pOwner
							);
							bufferSegments->push_back(tempBSeg);
							// No need to loop. Looping would cause two extra operations.
							// The minimum size of each buffer segment items array is always more than 1 (in any case
							// like networking, mobile data, terminal I/O, file I/O
							// Just use this newly created buffer segment
							{
								std::lock_guard<std::mutex> lock(*(tempBSeg->writerMutex));
								tempBSeg->inRead = false;
								tempBSeg->inWrite = true;
								(tempBSeg->items)[(tempBSeg->writingIndex)++] = item;
								tempBSeg->inRead = false;
								tempBSeg->inWrite = false;
							}
						}
						else {
							/*
							 * The buffer is a linear data structure (a list (probably a forward list)). So we do not
							 * need to iterate over the list of segments owned by the owner, rather move ahead with
							 * the last one in the list.
							*/
							BufferSegment<T>* lastSeg = bufferSegsOwned->back();
							// Write to buffer segment `lastSeg` only if it is writable
							if (lastSeg != nullptr) {
								if (lastSeg->isWritable()) {
									// Check the capacity of the current buffer segment
									if ((lastSeg->writingIndex) != (lastSeg->size)) {
										// writable
										// acquire lock on this buffer segment
										std::lock_guard<std::mutex> lock(*(lastSeg->writerMutex));
										lastSeg->inRead = false;	// Blocking read
										lastSeg->inWrite = true;
										// Write the item
										(lastSeg->items)[(lastSeg->writingIndex)++] = item;
										// reset the read and write permissions
										lastSeg->inRead = false;
										lastSeg->inWrite = false;
									}
									else {
										// Create a new buffer segment and write this item into that buffer
										BufferSegment<T>* tempBSeg = new BufferSegment<T>(
											lastSeg->size, pOwner
										);
										bufferSegments->push_back(tempBSeg);
										// acquire a lock on this latest buffer segment
										std::lock_guard<std::mutex> lock(*(tempBSeg->writerMutex));
										// set the read and write permissions
										tempBSeg->inRead = false;
										tempBSeg->inWrite = true;
										// Write in this new buffer segment
										(tempBSeg->items)[(tempBSeg->writingIndex)++] = item;
										// reset the read and write permissions
										tempBSeg->inRead = false;
										tempBSeg->inWrite = false;
									}
								}
							}
						}
					}
					else {
						std::ostringstream* oss = new std::ostringstream();
						(*oss) << "BufferSegmentOwner: 0x" << std::hex << reinterpret_cast<uintptr_t>(pOwner)
							<< "-wrong privilege->(REQUIRED: WRITE) on BufferSegment: Unkown";
						std::string msg = oss->str();
						delete oss;
						oss = nullptr;
						throw std::runtime_error(msg);
					}
					});
				(*th)->join();
			}
		}
		else {
			throw std::runtime_error("WRITE OP FAILED -- INVALID OWNER -- NULL -- INVALID OWNER UID");
		}
		return;
	}

	// TODO: Test this function
	/**
	* @brief Checks if there is next item avaialable to be read from the buffer.
	*
	* @return true if there are items to be read from the buffer.
	*/
	bool hasNext(BufferSegmentOwner* pOwner) {
		bool retVal{ false };
		// Check if the owner is a valid owner
		if (pOwner != nullptr) {
			// Check if there is a buffer segment with this owner
			typename std::list<BufferSegment<T>*>::iterator iter = bufferSegments->begin();
			// Advance to the buffer segment to be looked for readability
			while (iter != bufferSegments->end()) {
				// Find the buffer segment which has this owner and check if this buffer segment has the right
				// ownership
				if ((*iter)->doesOwnerExist(pOwner)) {
					// Check if the buffer segment have already been read upto the written size or not.
					if (
						(pOwner->bufferSegmentItemsArrayReadIndex) != (
							(((*iter)->writingIndex)) == -1 ? (*iter)->size : (*iter)->writingIndex
							)
						) {
						retVal = true;
					}
					else {
						retVal = false; // This is a necessary step as when we exceed the current buffer
						// segment to check another buffer segment, this retVal will be set to false again
						// so that the next buffer segment consisting the `writingIndex` or `size` can be
						// matched against `pOwner->bufferSegmentItemsArrayReadIndex` to know if there are
						// more items yet to be read or all have been read.
					}
				}
			}
		}
		else {
			throw std::runtime_error("ERR: OWNER NOT FOUND -- nullptr");
		}

		return retVal;
	}

	/**
 * @brief Reads the next item from the current/next buffer segment owned by pOwner
 */
	const T read(BufferSegmentOwner* pOwner) {
		try {
			// Validate owner pointer
			if (pOwner == nullptr) {
				throw std::runtime_error("ERR: OWNER NOT FOUND -- nullptr");
			}

			// Get the list of buffer segments owned by this owner
			std::list<BufferSegment<T>*>* segsInRead = bufferSegmentsOwned(pOwner);

			// Check if there is a buffer segment in the list to be read
			if(segsInRead == nullptr) {
				throw std::runtime_error("ERR: NO SUCH BUFFER ENTRY -- NO ASSOCIATED BUFFER");
			}
			else {
				// Advance by the `advanceBy` number of buffer segments already read till now
				unsigned long long advanceBy = pOwner->bufferSegmentReadIndex;
				typename std::list<BufferSegment<T>*>::iterator segInReadIterator = segsInRead->begin();
				while(advanceBy > 0) {
					++segInReadIterator;
					--advanceBy;
				}
				/*
				 * It is sure that `advanceBy` cannot touch 0 but can be zero(0) only when the owner is
				 * reading the first buffer segment.
				 */
				// Get the buffer segment
				BufferSegment<T>* segInRead = *segInReadIterator;
				// Check if there are any item left to be read in this buffer segment
				if((pOwner->bufferSegmentItemsArrayReadIndex) < (segInRead->size)) {
					// return and advance read index
					return segInRead->items[(pOwner->bufferSegmentItemsArrayReadIndex)++];
				}
				else {
					/*
					 * Move to the next buffer segment (if any) o/w throw a runtime error with buffer segment
					 * not found.
					 */
					// Check the iterator
					if(segInReadIterator == segsInRead->end()) {
						throw std::runtime_error("ERR: NO BUFFER ENTRY FOR OWNER : " + std::to_string((unsigned long long)((void*)pOwner)));
					}
					else {
						++(pOwner->bufferSegmentReadIndex);
						(pOwner->bufferSegmentItemsArrayReadIndex) = 0ULL;
						// advance the buffer segment iterator
						segInRead = *(++segInReadIterator);
						// return and advance read index
						return segInRead->items[(pOwner->bufferSegmentItemsArrayReadIndex)++];
					}
				}
			}

			// If no segment was found with the owner
			throw std::runtime_error("ERR: NO BUFFER ENTRY FOR OWNER : " + std::to_string((unsigned long long)((void*)pOwner)));
		}
		catch (const std::exception& e) {
			throw e; // Re-throw the caught exception
		}
	}


	// TODO: Test this function
	/**
	* @brief Reads all items from the buffer.
	*
	* Returns all items from the buffer owned by `pOwner`. Use while loop to get the next buffer segment's
	* items.
	*
	* @param pOwner Pointer to the owner of the buffer segments to be read.
	*
	* @return A dynamic sized array of type T with contents of the buffer segment copied to it.
	*/
	const T* read(BufferSegmentOwner* pOwner, unsigned long long bufferSegmentIndex) {
		/*
		* Unrestricted read except when the buffer segment is being written to. Thus there is no use of owner's
		* thread mutex here.
		*/
		try {
			// Validate owner pointer
			if (pOwner != nullptr) {
				// Check if there is a buffer segment with this owner
				typename std::list<BufferSegment<T>*>::iterator iter = bufferSegments->begin();
				// Advance to the buffer segment to be read
				unsigned long long advanceBy = pOwner->bufferSegmentReadIndex;
				while (iter != bufferSegments->end()) {
					// Find the buffer segment which has this owner and check if this buffer segment has the
					// right ownership.
					if ((*iter)->doesOwnerExist(pOwner)) {
						if (advanceBy < 0) {
							break;
						}
						else {
							--advanceBy;
						}
					}
				}
				if (iter != bufferSegments->end()) {
					++(pOwner->bufferSegmentReadIndex); // This segment has already been read, move to the next.
					return (*iter)->items;
				}
				throw std::runtime_error("NO ITEM FOUND -- END REACHED");
			}
			else {
				throw std::runtime_error("ERR: OWNER NOT FOUND -- nullptr");
			}
		}
		catch (const std::exception& e) {
			throw e;
		}
		return nullptr;
	}

	// TODO: Yet to be implemented
	/**
	* @brief Provides direct hook to the buffer segment's dynamic array for use with networking like Bluetooth,
	* TCP/IP etc. Generally used in `recv` and `send` functions in socket programming.
	*
	* WARNING: A POINTER TO THE BUFFER SEGMENT'S (WHOSE OWNER IS `pOwner`) DYNAMIC ARRAY WHICH HOUSES THE ACTUAL
	* DATA IS SENT TO THE CALLER.
	*/
	std::tuple<std::weak_ptr<T*>, int> bufferHookForWrite(BufferSegmentOwner* pOwner) {
		// Before proceeding with any chnages to the thread, obtain a mutex lock on the owner's thread
		std::lock_guard<std::mutex> lock(pOwner->ownerThreadMutex);
		std::tuple<std::weak_ptr<T*>, int> writerHook;
		// Get the old thread, wait for it to finish its task
		std::thread** ppOwnerThread = pOwner->getPPThread();
		std::thread* pOwnerThread = *ppOwnerThread;
		if (pOwnerThread->joinable()) {
			pOwnerThread->join();
			delete* ppOwnerThread;													// delete old thread
		}
		// Check for accessed index of the current buffer segment. If this buffer segment's `writingIndex` has
		// not exceeded the limit then return a pointer to the same buffer segment.
		// Else go to next buffer segment or create one if not available and return a hook to it.
		// Get the first buffer segment associated with this owner which is not in use
		// Associate new thread with function as observing changes in the buffer segment
		*ppOwnerThread = new std::thread([]() {
			// Observe the writingIndex
			});

		return std::move(writerHook);
	}

private:

	// Pruner threads related members
	ull intervalMS = 2000LL;							// This variable holds the time interval in milliseconds 
	// (default : 2000ms) after which prunning is performed.
	std::thread prunerThreadEngine;						// The pruner thread engine that handles the prunning
	// threads. TODO: Assign the prune function to this.
	std::list<std::thread*>* prunerThreads{ nullptr };

	std::list<BufferSegment<T>*>* bufferSegments{ nullptr };
	int previousDynamicBufferSize = 0;

	std::list<BufferSegment<T>*>* bufferSegmentsOwnedTemp{ nullptr };

	/**
	* @brief Get the list of pointers to buffer segments owned by the owner
	*
	* @param ppOnwer Pointer-to-Pointer to the owner of the buffer segment
	* @return Pointer to the list of `BufferSegment<T>**` containing the pointer to buffer segments owned by the
	* `pOwner` or nullptr if none.
	*/
	std::list<BufferSegment<T>*>* bufferSegmentsOwned(BufferSegmentOwner* pOwner) {


		bufferSegmentsOwnedTemp = new std::list<BufferSegment<T>*>();
		for (auto it = bufferSegments->begin(); it != bufferSegments->end(); ++it) {
			BufferSegment<T>* bSeg = *it;
			// Check if the given owner exists for this buffer segment
			if (bSeg->doesOwnerExist(pOwner)) {
				bufferSegmentsOwnedTemp->push_back(bSeg);
			}
			else {
				return nullptr;
			}
		}

		if (bufferSegmentsOwnedTemp->size() != 0) {
			return bufferSegmentsOwnedTemp;
		}
		else {
			delete bufferSegmentsOwnedTemp;
			bufferSegmentsOwnedTemp = nullptr;
			return nullptr;
		}

	}

	/**
	* @brief Employs the pruner threads to prune the irrelevant `BufferSegment`s. The "DynBufferThread" (the main
	* thread that is responsible for covering all the operations of a dynamic buffer inside it) spawns and destroys the
	* pruner threads according to the size of the dynamic buffer.
	*
	* The number of pruner threads will increase with increase in the size of the dynamic buffer.
	* The number of pruner threads will decrease with decrease in the size of the dynamic buffer.
	* Every time this function is called, it doesn't destroy the old pruner threads if the size of the buffer did not
	* change from the past check in an interval or when the size of dynamic buffer increases.
	*
	* Prunning is performed in an interval. The interval will either be shortened or increased as per the size.
	*/
	void prune() {
		// Check the size of bufferSegments from the previous size
		if (previousDynamicBufferSize == bufferSegments->size()) {
			// The number of pruner threads are not decreased.
			// Start prunning
		}
		else if (previousDynamicBufferSize < bufferSegments->size()) {
			// Decrease the number of pruner thrads and assign its buffer segment regions.
			// Start prunning
		}
		else {
			// Increase the number of pruner thrads and assign its buffer segment regions.
			// Start prunning
		}
		previousDynamicBufferSize = bufferSegments->size();
	}

	/**
	* @brief Get the iterator to the next buffer segment
	*
	* @param pOwner Pointer to the owner of the next buffer segment in search
	*
	* @return `std::list<BufferSegment<T>**>`'s iterator from the next `BufferSegment<T>**` owned by the `pOwner` or
	* bufferSegments->end() if none
	*/
	typename std::list<BufferSegment<T>*>::iterator iteratorToNextBufferSegment(
		typename std::list<BufferSegment<T>*>::iterator iteratorOnCurrentBufferSegment, BufferSegmentOwner* pOwner
	) {
		// Search for the next occurrence of a buffer segment owned by the `pOwner`
		if (iteratorOnCurrentBufferSegment == bufferSegments->end()) {
			return bufferSegments->end();												// Already at the end
		}
		typename std::list<BufferSegment<T>*>::iterator iter = iteratorOnCurrentBufferSegment;
		++iter;																			// Move to the next element

		for (; iter != bufferSegments->end(); ++iter) {
			if ((*iter)->doesOwnerExist(pOwner)) {
				return iter;
			}
		}

		return bufferSegments->end();
	}

	typename std::list<BufferSegment<T>*>::iterator iteratorToNextWritableBufferSegment(
		typename std::list<BufferSegment<T>*>::iterator iteartorOnCurrentBufferSegment, BufferSegmentOwner* pOwner
	) {
		// Search for the next occurence of a buffer segment owned by the `pOwner` and is writable
		if (iteartorOnCurrentBufferSegment == bufferSegments->end()) {
			return bufferSegments->end();												// Already at the end
		}
		typename std::list<BufferSegment<T>*>::iterator iter = iteratorToNextBufferSegment;
		++iter;																			// Move to the next element

		for (; iter != bufferSegments->end(); ++iter) {
			if ((*iter)->doesOwnerExist(pOwner) && (*iter)->isWritable()) {
				return iter;
			}
		}

		return bufferSegments->end();
	}

};

#endif