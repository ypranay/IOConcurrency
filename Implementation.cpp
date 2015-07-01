/* FileRangeLocking.cpp 
@Author: Pranay Yadav, EID Intern, GE Intelligent Platforms
@Description: Object Oriented Design for File IO using Byte Range Locking and Unlocking
*/
#include "stdafx.h"
#include <iostream>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <time.h>
#include <map>
#include <vector>
#include <utility>

#define MAX_HANDLES 256		// Maximum no. of Handles that can be allocated to the requesting threads.
#define BUFFSIZE 255	// Buffer size for the read and write IO.
#define DEFAULT -1		// State Variable to denote available Handle at the IOManager
#define NUMTHREADS 256	// No. of Spawned Threads
#define READ 0
#define WRITE 1
using namespace std;

const char* FileName = "Proficy-Perf_Data.txt";	/* Input File Name */
int* OP;
/*
	@Description: 
	To Compute System Time at the time when this method is called. Used as a performance counter. Returns double-formatted system time.
*/
double GetSecs(void){
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
	if(! QueryPerformanceFrequency(&frequency)) 
        printf("QueryPerformanceFrequency Failed\n");
	if(! QueryPerformanceCounter(&start))
        printf("QueryPerformanceCounter Failed\n");
    return (double)start.QuadPart * 1000. * (1. / (double)frequency.QuadPart);
}

/*
	IOThread class which encapsulates the ThreadID and the operation which the thread is requesting.
		0 - Read, 1- Write.
*/
class IOThread{
	int op;
	int threadID;
public:
	IOThread(int o,int id):op(o),threadID(id){}		// Classic C'Tor for the class
	int getIO() const{return op;}		// Classic GET methods for private members of the class
	int getThreadID() const{ return threadID;}
};

/*
	IO Manager Class which is central to the OOP concept. Handles request from the threads for IO.
*/
class IOManager{		
	vector<int> availableHandle;		// State Vector for the availability of the File Handles
	HANDLE fileHandles[MAX_HANDLES];	// Array of File Handles to the File
	map<pair<int,int>,vector<IOThread>> rangeLockMap; // Map containing Byte ranges over which lock has been established as keys and the list of 
													  // IOThreads which are performing IO on the intersecting byte ranges. 
public:
	/*
	@Description: Default Constructor to allocate MAX_HANDLES number of HANDLES to the file. Initialized all Handles to be available initially.
	*/
	IOManager(){		//Default operation 
		availableHandle.resize(MAX_HANDLES,-1);
	}
	
	/*
	@Description:
	Request for Lock in the given byte range. For readers request a Shared Lock, otherwise Exclusive Lock. 
	For detailed explanation, refer to Description of the RequestIO() method.
	Writer awaits until the Readers have completed their execution.
	To avoid starvation of Writer, no new Read requests for the intersecting byte ranges can be attempted before this Write request is
	completed. This is yet to be implemented.
	
	This method blocks the thread until the Lock is granted. (Synchronous)
	Returns true if successful, false otherwise.

	@param handle: HANDLE to the file which is to be locked.
	@param thread: IOThread object which is requesting the lock
	@param range: byte range over which lock is requested
	*/
	bool AcquireRangeLock(HANDLE handle,IOThread thread,pair<int,int> range){

		/*TODO 
		Make a request queue in which new threads will be queued. If the incoming is read and there is write before on the intersecting
		range then block this one.This is to avoid starvation of Writers when Readers have priority. This will block any more readers to request
		for lock until all the Writers which had requested a lock earlier have been granted the lock.*/

		/*while(numOfWriteThreads(conflictIOThreads)!=0){
			//Do Nothing
		}*/

		OVERLAPPED o;	// Create an OVERLAPPED object to seek the byte range
		o.Offset = 0;
		o.OffsetHigh = 0;
		o.hEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
		o.Internal = 0;
		o.InternalHigh = 0;
		o.Pointer = 0 ;
		switch(thread.getIO()){
		case READ:
			if(LockFileEx(handle,0,0,BUFFSIZE,0,&o))		// Obtain a SHARED lock
				return true;
			return false;
		case WRITE:
			if(LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK,0,BUFFSIZE,0,&o))		//Obtain a exclusive lock
				return true;
			return false;
		}
	}

	/*
	@Description:
	Release the Lock over the range in the function argument. Returns true if successful, false otherwise.
	Blocks the thread until unlocking.
	
	@param handle: HANDLE to the file where the byte range is to be unlocked
	@param range: Byte range which is to be unlocked
	*/
	bool ReleaseRangeLock(HANDLE handle,pair<int,int> range){
		OVERLAPPED o;		// Create a OVERLAPPED object for releasing the lock on that byte range
		o.Offset = 0;
		o.OffsetHigh = 0;
		o.hEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
		o.Internal = 0;
		o.InternalHigh = 0;
		o.Pointer = 0 ;
		if(UnlockFileEx(handle,0,BUFFSIZE,0,&o))
			return true;
		return false;
	}
	
	/*
	@Description:
	Reads from File Range. Returns true when successful, false otherwise.

	@param hFile: HANDLE of the file which is to be read from 
	@param range: pair containing the byte range from which the buffer will be filled
	@param writeBuffer: Buffer to be populated after reading from the File
	*/
	bool FileRead(HANDLE hFile, pair<int,int> range, char* readBuffer){
		DWORD numRead=0;
		SetFilePointer(hFile,range.first,0,FILE_BEGIN);	// Seek the file pointer to the desired location
		if(!ReadFile(hFile,readBuffer,BUFFSIZE,&numRead,NULL))
			return false;
		readBuffer[BUFFSIZE]='\0';
		return true;
	}

	/*
	@Description:
	Writes to File Range. Returns true when successful, false otherwise.

	@param hFile: HANDLE of the file which is to be written to
	@param range: pair containing the byte range to which the buffer will be written to
	@param writeBuffer: Buffer to be written to the File
	*/
	bool FileWrite(HANDLE hFile, pair<int,int> range, char* writeBuffer){
		DWORD numWritten=0;
		SetFilePointer(hFile,range.first,0,FILE_BEGIN);	// Seek the file pointer to the desired location
		if(!WriteFile(hFile,writeBuffer,BUFFSIZE,&numWritten,NULL))
			return false;
		return true;
	}

	/*
	@Description: 
	Returns a pair containing the free HANDLE and its offset in the vector containing all the HANDLES, for use by the requesting thread

	@param io: IO of the requesting thread so as to mark in the vector.
	*/
	pair<HANDLE,int> requestHandle(int io){
		vector<int>::iterator it;
		
		//Locate first free Handle, i.e. value stored at that index should be -1.
		for(it= availableHandle.begin(); it!= availableHandle.end() && (*it) != -1; ++it);
	
		// If no such index found, flag freeHandle as -1
		int freeHandle= (it==availableHandle.end()?-1:it-availableHandle.begin());
		
		if(freeHandle==-1) // Unsuccessful attempt to find free Handle
			return make_pair((HANDLE)NULL,-1);
		
		//Found Free Handle
		fileHandles[freeHandle] = CreateFile((LPCWSTR)FileName,GENERIC_READ | GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_RANDOM_ACCESS|FILE_ATTRIBUTE_TEMPORARY,NULL); 
		
		if(fileHandles[freeHandle] == INVALID_HANDLE_VALUE) // Could Not Open File Handle
			return make_pair((HANDLE)NULL,-1);

		availableHandle[freeHandle]=io;	// Mark index as occupied with operation "io", same of that as this thread.
		return make_pair(fileHandles[freeHandle],freeHandle);
	}

	/*
	@Description:
	Close the Handle available with the IOManager and marks the vector as -1 (meaning available to be taken by another Handle)
	Returns true when successful, false otherwise

	@param handleToBeClosed :	pair.first is the HANDLE which is to be closed and pair.second is the offset of that HANDLE in the vector 
								available with the IOManager object.
	*/
	bool closeHandle(pair<HANDLE,int> handleToBeClosed){
		if(!CloseHandle(handleToBeClosed.first))
			return false;
		availableHandle[handleToBeClosed.second]=DEFAULT;
		return true;
	}

	/*
	@Description:
	Spawned thread requests IO through this method. Stages involved are: 
		1. Request the IOManager for a valid and free handle from a list of handles available at the IOManager's end. Busy waiting until obtained.
		2. For Readers: 
				Acquire a shared lock on the range. This lock is required so as to disallow other writers (which seek Exclusive lock) on the
				Byte Ranges which intersect the requested Byte Range.
		3. For Writers:
				Acquire an exclusive lock on the range. This lock is required so as to disallow other readers (which seek shared lock)
				/writers (which seek Exclusive lock) on the byte ranges which intersect the requested byte range.
		4. Compatibility Matrix for the Range Locking Mechanism (only for ranges which are intersecting, always allowed for others):
								Shared		Exclusive
				Shared			Allow		Disallow
				Exclusive		Disallow	Disallow
		5. IO Performed
		6. Unlock the byte ranges
		7. Release the File Handle for use by another thread.

		@return VOID
	@param thread: IOThread object containing info about the requesting thread
	@param offset: integer which acts as the lower boundary of the range for which IO is requested
	@param numBytes: integer window size over which IO is requested
	
	Note: All the methods are synchronous in nature. Executing doesnt proceed until any of the stages are not completed.
	*/
	void requestIO(IOThread thread, int offset, int numBytes){
		int lowByteOffset= offset, highByteOffset = offset+numBytes-1;	 // IO on the range [offset, offset+numBytes-1]
		pair<HANDLE,int> obtainedHandle;
		
		do{	// Loop until the Thread gets a valid and free Handle to use!!
			obtainedHandle = requestHandle(thread.getIO());
		}while(obtainedHandle.second==-1 && obtainedHandle.first==NULL);	// Check if Handle is invalid 
		//Successfully Obtained Handle for IO!!
		
		switch(thread.getIO()){
		case READ:	//Read Thread
			
			if(!AcquireRangeLock(obtainedHandle.first,thread,make_pair(lowByteOffset,highByteOffset)))
				return;
			//Successfully Acquired Exclusive Lock!!
			
			char readBuffer[BUFFSIZE+1]; // Populated Buffer after Reading
			
			if(!FileRead(obtainedHandle.first,make_pair(lowByteOffset,highByteOffset),readBuffer))
				return;
			//Successfully Read from Range!!
			
			if(!ReleaseRangeLock(obtainedHandle.first,make_pair(lowByteOffset,highByteOffset)))
				return;
			//Successfully Unlocked Range!!
			
			if(!closeHandle(obtainedHandle))
				return;
			//Successfully Closed Handle!!
			break;
		case WRITE:	//Write Thread
			
			if(!AcquireRangeLock(obtainedHandle.first,thread,make_pair(lowByteOffset,highByteOffset)))
				return;
			//Successfully Acquired Exclusive Lock!!
			
			char writeBuffer[BUFFSIZE]= " "; // Buffer to be Written to the Range!!
			
			if(!FileWrite(obtainedHandle.first,make_pair(lowByteOffset,highByteOffset),readBuffer))
				return;
			//Successfully Written to Range!!
			
			if(!ReleaseRangeLock(obtainedHandle.first,make_pair(lowByteOffset,highByteOffset)))
				return;
			//Successfully Unlocked Range!!
			
			if(!closeHandle(obtainedHandle))
				return;
			//Successfully Closed Handle!!
			break;
		}
	}
}iomanager;	// Global IOManager object


/*
@Description: Method called by the thread spawned. Looks up the OP array and requests that IO. 

@param ThreadNo: int containing the ThreadID of the calling thread (0-offset IDs)
*/
DWORD WINAPI WorkerThread(LPVOID ThreadNo){
	int op = OP[(int)ThreadNo];
	IOThread newthread(op,(int)ThreadNo);		//Create a wrapper object for the requesting thread
	int factor = (int)(((double)(rand()%10)/10.0)*10);	 // Randomizing the offset for the thread
	int offset = factor* BUFFSIZE;		
	iomanager.requestIO(newthread,offset,BUFFSIZE);
	return 0;
}


/*
@Description: Method which fills up the operation for each thread in the OP array

@param choice:	For i) When only Readers are considered, choice=0
				For ii) When only Writers are considered, choice=1
				For iii) When Readers are made to increase from 1 to 256, choice =2
				For iv) When Writers are made to increase from 1 to 256, choice=3
@param size:	size of the OP array to be filled
@param hopsize:		For cases iii) and iv) as mentioned above, Readers for case iii (Writers for case iv) are marked at indexes (0, hopsize, 2*hopsize ... size)
					where hopsize depends on the proportion of the readers to the total no. of threads.
*/
void generator(int choice,int size, int hopsize=0){
	switch(choice){
	case 0:
		fill_n(OP,size,0);
		return;
	case 1:
		fill_n(OP,size,1);
		return;
	case 2:
		if(hopsize==1){
			fill_n(OP,size,0);
			return;
		}
		fill_n(OP,size,1);
		for(int i=0; i< size; i+=hopsize)
			OP[i]=0;
		return;
	case 3:
		if(hopsize==1){
			fill_n(OP,size,1);
			return;
		}
		fill_n(OP,size,0);
		for(int i=0; i< size; i+=hopsize)
			OP[i]=1;
		return;
	}
}

/*
Main method

VERY IMPORTANT: Please comment out the remaining 3 loops when running one of the loops. This is done because of some latent errors which popped up
execution, and I had no clue why they were happening. Like, exception was thrown at line where vector iterator was checked for equality with 
container's end iterator etc.

So a hacky fix is to comment out the remaining 3 loops while running of the loops.
*/

int _tmain(int argc, _TCHAR* argv[])
{
	srand(time(NULL));

	int TotalNoThreads= 1, index=0, numReaders=0, numWriters=0;
	double stdtimeperthread = 0,	// Running Time for reading by single thread alone
			stdtimeforreader=0,		// Running Time for execution by single reader, later used for the case where both R&W are happening
			stdtimeforwriter=0;		// Running Time for execution by single writer, later used for the case where both R&W are happening

	char resultFileName[255];
	sprintf(resultFileName,"results.txt",numReaders,numWriters);
	FILE* resultFile = fopen(resultFileName, "w");
	char tx[255];
	memset(tx,0,sizeof(tx));
	sprintf(tx,"Readers Only!!\n(# of Readers, Running-time)\n");
	std::fprintf(resultFile,tx);
					
	//Increasing # of Readers (without Writers)
	for(int TotalNoThreads=1, index=0; TotalNoThreads<=256; TotalNoThreads *= 2, index++){
		OP = new int[TotalNoThreads];
		generator(0,TotalNoThreads);
		numReaders= TotalNoThreads;
		numWriters=0;
		
		double start = GetSecs();		// Starting the time 
		HANDLE* pThread = new HANDLE[numReaders];	// Allocating Handles for generating Threads
		for (int i = 0; i < numReaders; i++) 
			pThread[i] = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)WorkerThread,(LPVOID)i,0,NULL); // Spawning Threads
		WaitForMultipleObjects(numReaders,pThread,TRUE,INFINITE);	// Wait for Threads to finish their execution
		
		double runtime = (GetSecs()-start)/1000.0;	// Total execution time
		memset(tx,0,sizeof(tx));
		stdtimeperthread=(TotalNoThreads==1?runtime:stdtimeperthread);
		stdtimeforreader=stdtimeperthread;
		sprintf(tx,"With Byte Range Locking: (%d,%.4f)\n",numReaders,runtime);
		std::fprintf(resultFile,tx);
		memset(tx,0,sizeof(tx));
		sprintf(tx,"Without Byte Range Locking: (%d,%.4f)\n",numReaders,stdtimeperthread*numReaders);
		std::fprintf(resultFile,tx);
		
		for (int i = 0; i < numReaders; i++) 
			CloseHandle(pThread[i]);
		
		delete[] pThread;
		delete[] OP;
		fclose(resultFile);
	}
	
	
	// For increasing # of Writers  (No Readers)
	memset(tx,0,sizeof(tx));
	sprintf(tx,"Writers Only!!\n(# of Writers, Running-time)\n");
	std::fprintf(resultFile,tx);
	for(int TotalNoThreads=1, index=0; TotalNoThreads<=256; TotalNoThreads *= 2, index++){
		OP= new int[TotalNoThreads];
		generator(1,TotalNoThreads);
		numWriters=TotalNoThreads;
		numReaders=0;
		
		double start = GetSecs();		// Starting the time 
		HANDLE* pThread = new HANDLE[numWriters];	// Allocating Handles for generating Threads
		for (int i = 0; i < numWriters; i++) 
			pThread[i] = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)WorkerThread,(LPVOID)i,0,NULL); // Spawning Threads
		WaitForMultipleObjects(numWriters,pThread,TRUE,INFINITE);	// Wait for Threads to finish their execution
		
		char resultFileName[255];
		memset(tx,0,sizeof(tx));
		
		double runtime = (GetSecs()-start)/1000.0;	// Total execution time
		stdtimeperthread=(numWriters==1?runtime:stdtimeperthread);
		stdtimeforwriter= stdtimeperthread;
		sprintf(tx,"With Byte Range Locking: (%d,%.4f)\n",numWriters,runtime);
		std::fprintf(resultFile,tx);
		memset(tx,0,sizeof(tx));
		sprintf(tx,"Without Byte Range Locking: (%d,%.4f)\n",numWriters,stdtimeperthread*numWriters);
		std::fprintf(resultFile,tx);
		
		for (int i = 0; i < numWriters; i++) 
			CloseHandle(pThread[i]);
		delete[] pThread;
		delete[] OP;
		fclose(resultFile);
	}
	
	//Total # of Threads is fixed to NUM_HANDLES, and # of Readers is increased exponentially
	memset(tx,0,sizeof(tx));
	sprintf(tx,"Readers Increasing, Writers decreasing (Total # of Threads = NUM_THREADS) !!\n(# of Readers, Running-time)\n");
	std::fprintf(resultFile,tx);
	
	for(int numReaders=1, index=0; numReaders<=256; numReaders *= 2, index++){
		OP = new int[NUMTHREADS];
		generator(2,NUMTHREADS,NUMTHREADS/numReaders);
		numWriters= 256-numReaders;

		double start = GetSecs();		// Starting the time 
		HANDLE* pThread = new HANDLE[NUMTHREADS];	// Allocating Handles for generating Threads
		for (int i = 0; i < NUMTHREADS; i++) 
			pThread[i] = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)WorkerThread,(LPVOID)i,0,NULL); // Spawning Threads
		WaitForMultipleObjects(NUMTHREADS,pThread,TRUE,INFINITE);	// Wait for Threads to finish their execution
		
		memset(tx,0,sizeof(tx));
		
		double runtime = (GetSecs()-start)/1000.0;	// Total execution time
		double stdtime = (numReaders* stdtimeforreader + numWriters* stdtimeforwriter); // Approzimated by time taken when that many readers and that many writers will R or W in order and not concurrently.
		sprintf(tx,"With Byte Range Locking: (%d,%.4f)\n",numReaders,runtime);
		std::fprintf(resultFile,tx);
		memset(tx,0,sizeof(tx));
		sprintf(tx,"Without Byte Range Locking: (%d,%.4f)\n",numReaders,stdtime);
		std::fprintf(resultFile,tx);
		
		for (int i = 0; i < NUMTHREADS; i++) 
			CloseHandle(pThread[i]);
		delete[] pThread;
		delete[] OP;
		fclose(resultFile);
	}
	
	//Total # of Threads is fixed to NUM_HANDLES, and # of Writers is increased exponentially
	sprintf(tx,"Writers Increasing, Readers decreasing (Total # of Threads = NUM_THREADS) !!\n(# of Writers, Running-time)\n");
	for(int numWriters=1, index=0; numWriters<=256; numWriters *= 2, index++){
		OP = new int[NUMTHREADS];
		generator(3,NUMTHREADS,NUMTHREADS/numWriters);
		numReaders= 256-numWriters;
		
		double start = GetSecs();		// Starting the time 
		HANDLE* pThread = new HANDLE[NUMTHREADS];	// Allocating Handles for generating Threads
		for (int i = 0; i < NUMTHREADS; i++) 
			pThread[i] = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)WorkerThread,(LPVOID)i,0,NULL); // Spawning Threads
		WaitForMultipleObjects(NUMTHREADS,pThread,TRUE,INFINITE);	// Wait for Threads to finish their execution
		
		memset(tx,0,sizeof(tx));
		double runtime = (GetSecs()-start)/1000.0;	// Total execution time
		double stdtime = (numReaders* stdtimeforreader + numWriters* stdtimeforwriter);
		sprintf(tx,"With Byte Range Locking: (%d,%.4f)\n",numWriters,runtime);
		std::fprintf(resultFile,tx);
		memset(tx,0,sizeof(tx));
		sprintf(tx,"Without Byte Range Locking: (%d,%.4f)\n",numWriters,stdtime);
		std::fprintf(resultFile,tx);
		
		for (int i = 0; i < NUMTHREADS; i++) 
			CloseHandle(pThread[i]);
		delete[] pThread;
		delete[] OP;
		fclose(resultFile);
	}
	getchar();
	return 0;
}

