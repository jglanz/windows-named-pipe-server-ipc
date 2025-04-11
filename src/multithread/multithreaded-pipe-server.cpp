#include <csignal>
#include <iostream>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include <thread>

#define BUFSIZE 512

DWORD WINAPI InstanceThread(LPVOID);
VOID GetAnswerToRequest(LPTSTR, LPTSTR, LPDWORD);

using namespace std::chrono_literals;

std::atomic<bool> running(true);


// Signal handler for SIGINT
static void handleSignal(int signal) {
  if (signal == SIGINT) {
    std::cout << "\nSIGINT received. Exiting gracefully...\n";
    running = false;
  }
}


int _tmain(VOID)
{
   BOOL   fConnected = FALSE;
   DWORD  dwThreadId = 0;
   HANDLE hPipe = INVALID_HANDLE_VALUE, hThread = nullptr;
   LPCTSTR lpszPipename = TEXT("\\\\.\\pipe\\mypipe");


   std::signal(SIGINT, handleSignal);

// The main loop creates an instance of the named pipe and
// then waits for a client to connect to it. When the client
// connects, a thread is created to handle communications
// with that client, and this loop is free to wait for the
// next client connect request. It is an infinite loop.

   for (;;)
   {
      _tprintf( TEXT("\nPipe Server: Main thread awaiting client connection on %s\n"), lpszPipename);
      hPipe = CreateNamedPipe(
          lpszPipename,             // pipe name
          PIPE_ACCESS_DUPLEX,       // read/write access
          PIPE_TYPE_MESSAGE |       // message type pipe
          PIPE_READMODE_MESSAGE |   // message-read mode
          PIPE_WAIT,//PIPE_WAIT,                // blocking mode
          PIPE_UNLIMITED_INSTANCES, // max. instances
          BUFSIZE,                  // output buffer size
          BUFSIZE,                  // input buffer size
          0,                        // client time-out
          nullptr
      );                    // default security attribute

      if (hPipe == INVALID_HANDLE_VALUE)
      {
          _tprintf(TEXT("CreateNamedPipe failed, GLE=%d.\n"), GetLastError());
          return -1;
      }

      // Wait for the client to connect; if it succeeds,
      // the function returns a nonzero value. If the function
      // returns zero, GetLastError returns ERROR_PIPE_CONNECTED.

      fConnected = ConnectNamedPipe(hPipe, nullptr) ?
         TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

      if (fConnected)
      {
         printf("Client connected, creating a processing thread.\n");

         // Create a thread for this client.
         hThread = CreateThread(
           nullptr,              // no security attribute
            0,                 // default stack size
            InstanceThread,    // thread proc
            (LPVOID) hPipe,    // thread parameter
            0,                 // not suspended
            &dwThreadId);      // returns thread ID

         if (hThread == nullptr)
         {
            _tprintf(TEXT("CreateThread failed, GLE=%d.\n"), GetLastError());
            return -1;
         }
         else CloseHandle(hThread);
       }
      else
        // The client could not connect, so close the pipe.
         CloseHandle(hPipe);
   }

   return 0;
}

DWORD WINAPI InstanceThread(LPVOID lpvParam)
// This routine is a thread processing function to read from and reply to a client
// via the open pipe connection passed from the main loop. Note this allows
// the main loop to continue executing, potentially creating more threads of
// of this procedure to run concurrently, depending on the number of incoming
// client connections.
{
   HANDLE hHeap      = GetProcessHeap();
   TCHAR* pchRequest = (TCHAR*)HeapAlloc(hHeap, 0, BUFSIZE*sizeof(TCHAR));
   TCHAR* pchReply   = (TCHAR*)HeapAlloc(hHeap, 0, BUFSIZE*sizeof(TCHAR));

   DWORD cbBytesRead = 0, cbReplyBytes = 0, cbWritten = 0;
   BOOL fSuccess = FALSE;
   HANDLE hPipe  = nullptr;

   // Do some extra error checking since the app will keep running even if this
   // thread fails.

   if (lpvParam == nullptr)
   {
       printf( "\nERROR - Pipe Server Failure:\n");
       printf( "   InstanceThread got an unexpected NULL value in lpvParam.\n");
       printf( "   InstanceThread exitting.\n");
       if (pchReply != nullptr) HeapFree(hHeap, 0, pchReply);
       if (pchRequest != nullptr) HeapFree(hHeap, 0, pchRequest);
       return (DWORD)-1;
   }

   if (pchRequest == nullptr)
   {
       printf( "\nERROR - Pipe Server Failure:\n");
       printf( "   InstanceThread got an unexpected NULL heap allocation.\n");
       printf( "   InstanceThread exitting.\n");
       if (pchReply != nullptr) HeapFree(hHeap, 0, pchReply);
       return (DWORD)-1;
   }

   if (pchReply == nullptr)
   {
       printf( "\nERROR - Pipe Server Failure:\n");
       printf( "   InstanceThread got an unexpected NULL heap allocation.\n");
       printf( "   InstanceThread exitting.\n");
       if (pchRequest != nullptr) HeapFree(hHeap, 0, pchRequest);
       return (DWORD)-1;
   }

   // Print verbose messages. In production code, this should be for debugging only.
   printf("InstanceThread created, receiving and processing messages.\n");

// The thread's parameter is a handle to a pipe object instance.

   hPipe = (HANDLE) lpvParam;

// Loop until done reading
   while (1)
   {
   // Read client requests from the pipe. This simplistic code only allows messages
   // up to BUFSIZE characters in length.
   //    fSuccess = ReadFile(
   //       hPipe,        // handle to pipe
   //       pchRequest,    // buffer to receive data
   //       BUFSIZE*sizeof(TCHAR), // size of buffer
   //       &cbBytesRead, // number of bytes read
   //       NULL);        // not overlapped I/O
   //
   //    if (!fSuccess || cbBytesRead == 0)
   //    {
   //        if (GetLastError() == ERROR_BROKEN_PIPE)
   //        {
   //            _tprintf(TEXT("InstanceThread: client disconnected.\n"));
   //        }
   //        else
   //        {
   //            _tprintf(TEXT("InstanceThread ReadFile failed, GLE=%d.\n"), GetLastError());
   //        }
   //        break;
   //    }
   //
   // // Process the incoming message.
   //    GetAnswerToRequest(pchRequest, pchReply, &cbReplyBytes);

   // Write the reply to the pipe.
     std::uint32_t counter{0};
     DWORD dwWritten;
     while (running) {
       ++counter;
       std::string s = std::format("Count is {}", counter);
       if (!WriteFile(hPipe, s.c_str(), s.length(), &dwWritten, nullptr)) {
         std::cerr << "Error writing to pipe: " << GetLastError() << std::endl;
         return 1;
       }

       if (counter % 100 == 0) {
         std::cout << std::format("Sent {0} messages", counter) << std::endl;
       }
       std::this_thread::sleep_for(100ms);
     }
     //
     // fSuccess = WriteFile(
     //     hPipe,        // handle to pipe
     //     pchReply,     // buffer to write from
     //     cbReplyBytes, // number of bytes to write
     //     &cbWritten,   // number of bytes written
     //     NULL);        // not overlapped I/O
     //
     //  if (!fSuccess || cbReplyBytes != cbWritten)
     //  {
     //      _tprintf(TEXT("InstanceThread WriteFile failed, GLE=%d.\n"), GetLastError());
     //      break;
     //  }
  }

// Flush the pipe to allow the client to read the pipe's contents
// before disconnecting. Then disconnect the pipe, and close the
// handle to this pipe instance.

   FlushFileBuffers(hPipe);
   DisconnectNamedPipe(hPipe);
   CloseHandle(hPipe);

   HeapFree(hHeap, 0, pchRequest);
   HeapFree(hHeap, 0, pchReply);

   printf("InstanceThread exiting.\n");
   return 1;
}

VOID GetAnswerToRequest( LPTSTR pchRequest,
                         LPTSTR pchReply,
                         LPDWORD pchBytes )
// This routine is a simple function to print the client request to the console
// and populate the reply buffer with a default data string. This is where you
// would put the actual client request processing code that runs in the context
// of an instance thread. Keep in mind the main thread will continue to wait for
// and receive other client connections while the instance thread is working.
{
    _tprintf( TEXT("Client Request String:\"%s\"\n"), pchRequest );

    // Check the outgoing message to make sure it's not too long for the buffer.
    if (FAILED(StringCchCopy( pchReply, BUFSIZE, TEXT("default answer from server") )))
    {
        *pchBytes = 0;
        pchReply[0] = 0;
        printf("StringCchCopy failed, no outgoing message.\n");
        return;
    }
    *pchBytes = (lstrlen(pchReply)+1)*sizeof(TCHAR);
}