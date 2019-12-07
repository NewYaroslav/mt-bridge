// *******************************************************************************
// Socket library. NOT FULLY TESTED, to put it mildly.
//
// Features:
// 
//   * Both client and server sockets
//   * Both send and receive
//   * Both MT4 and MT5 (32-bit and 64-bit)
//
// The support for both 32-bit and 64-bit MT5 involves some horrible steps
// because MT4/5 does not have an integer data type whose size varies
// depending on the platform; no equivalent to the Win32 INT_PTR etc.
// As a result, it's necessary to have two versions of most of the Winsock
// DLL imports, involving a couple of horrible tweaks, plus two paths of 
// execution for 32/64-bit. It's also necessary to handle the Winsock hostent
// structure differently.
//
// A ClientSocket() connection to a server can either be to a port on
// localhost, or to a port on a remote hostname/IP address, using different
// constructors. After creating ClientSocket(), and periodially thereafter,
// you should check IsSocketConnected(). If it returns false, then you
// need to destroy that instance of the ClientSocket() class and create a new one.
//
// The ClientSocket() has simple Send() and Receive() members. The latter can
// either deliver any raw data sitting on the socket since the last call
// to Receive(), or you an specify a message terminator (such as \r\n), in which
// case the function will only return you complete messages, once available.
// You'll typically want to call Receive() from OnTimer().
//
// A ServerSocket() which waits for client connections is given a port number
// to listen on, and a boolean parameter indicating whether it should
// accept connections from localhost only, or from any machine. After creating
// the instance of ServerSocket(), you should check the Created() function to
// make sure that the initialisation worked. The main possible reason for
// failure is that something else is already listening on your chosen port.
// Note that if your EA terminates without deleting/releasing an instance
// of ServerSocket(), then the port will not be released until you
// shut down MT4/5. You need to make very sure that destroy instances
// of ServerSocket(), e.g. in OnDeinit().
//
// You accept connections from pending clients by calling Accept(). You will
// usually want to do this in OnTimer(). What you get back is either NULL, if
// there is no pending connection, or an instance of ClientSocket() which you
// can then use to communicate with the client.
// 
// *******************************************************************************
// 07.12.2019
// Added ClientSocket method for sending raw data - SendRaw
// *******************************************************************************

#property strict


// -------------------------------------------------------------
// WinInet constants and structures
// -------------------------------------------------------------

#define SOCKET_HANDLE32       uint
#define SOCKET_HANDLE64       ulong
#define AF_INET               2
#define SOCK_STREAM           1
#define IPPROTO_TCP           6
#define INVALID_SOCKET32      0xFFFFFFFF
#define INVALID_SOCKET64      0xFFFFFFFFFFFFFFFF
#define SOCKET_ERROR          -1
#define INADDR_NONE           0xFFFFFFFF
#define FIONBIO               0x8004667E
#define WSAWOULDBLOCK         10035

struct sockaddr {
   short family;
   ushort port;
   uint address;
   ulong ignore;
};

// -------------------------------------------------------------
// DLL imports
// -------------------------------------------------------------

#import "ws2_32.dll"
   // Imports for 32-bit environment
   SOCKET_HANDLE32 socket(int, int, int);
   int connect(SOCKET_HANDLE32, sockaddr&, int);
   int closesocket(SOCKET_HANDLE32);
   int send(SOCKET_HANDLE32, uchar&[],int,int);
   int recv(SOCKET_HANDLE32, uchar&[], int, int);
   int ioctlsocket(SOCKET_HANDLE32, uint, uint&);
   int bind(SOCKET_HANDLE32, sockaddr&, int);
   int listen(SOCKET_HANDLE32, int);
   SOCKET_HANDLE32 accept(SOCKET_HANDLE32, int, int);
   
   // Imports for 64-bit environment
   SOCKET_HANDLE64 socket(int, int, uint);
   int connect(SOCKET_HANDLE64, sockaddr&, int);
   int closesocket(SOCKET_HANDLE64);
   int send(SOCKET_HANDLE64, uchar&[], int, int);
   int recv(SOCKET_HANDLE64, uchar&[], int, int);
   int ioctlsocket(SOCKET_HANDLE64, uint, uint&);
   int bind(SOCKET_HANDLE64, sockaddr&, int);
   int listen(SOCKET_HANDLE64, int);
   SOCKET_HANDLE64 accept(SOCKET_HANDLE64, int, int);

   // Neutral; no difference between 32-bit and 64-bit
   uint inet_addr(uchar&[]);
   uint gethostbyname(uchar&[]);
   ulong gethostbyname(char&[]);
   int WSAGetLastError();
   uint htonl(uint);
   ushort htons(ushort);
#import

// For navigating the hostent structure, with indescribably horrible variation
// between 32-bit and 64-bit
#import "kernel32.dll"
   void RtlMoveMemory(uint&, uint, int);
   void RtlMoveMemory(ushort&, uint, int);
   void RtlMoveMemory(ulong&, ulong, int);
   void RtlMoveMemory(ushort&, ulong, int);
#import


// -------------------------------------------------------------
// Client socket class
// -------------------------------------------------------------

class ClientSocket
{
   private:
      // Need different socket handles for 32-bit and 64-bit environments
      SOCKET_HANDLE32 mSocket32;
      SOCKET_HANDLE64 mSocket64;
      
      // Other state variables
      bool mConnected;
      int mLastWSAError;
      string mPendingReceiveData;
                    
   public:
      ClientSocket(ushort localport);
      ClientSocket(string HostnameOrIPAddress, ushort localport);

      ClientSocket(SOCKET_HANDLE32 clientsocket32);
      ClientSocket(SOCKET_HANDLE64 clientsocket64);

      ~ClientSocket();
      bool Send(string strMsg);
      bool SendRaw(uchar &buffer[], const int buffer_size);
      string Receive(string MessageSeparator = "");
      
      bool IsSocketConnected() {return mConnected;}
      int GetLastSocketError() {return mLastWSAError;}
};


// -------------------------------------------------------------
// Constructor for a simple connection to 127.0.0.1
// -------------------------------------------------------------

ClientSocket::ClientSocket(ushort localport)
{
   // Need to create either a 32-bit or 64-bit socket handle
   mConnected = false;
   mLastWSAError = 0;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      uint proto = IPPROTO_TCP;
      mSocket64 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket64 == INVALID_SOCKET64) {
         mLastWSAError = WSAGetLastError();
         return;
      }
   } else {
      int proto = IPPROTO_TCP;
      mSocket32 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket32 == INVALID_SOCKET32) {
         mLastWSAError = WSAGetLastError();
         return;
      }
   }
   
   // Fixed definition for connecting to 127.0.0.1, with variable port
   sockaddr server;
   server.family = AF_INET;
   server.port = htons(localport);
   server.address = 0x100007f; // 127.0.0.1
   
   // connect() call has to differ between 32-bit and 64-bit
   int res;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      res = connect(mSocket64, server, sizeof(sockaddr));
   } else {
      res = connect(mSocket32, server, sizeof(sockaddr));
   }
   if (res == SOCKET_ERROR) {
      // Oops
      mLastWSAError = WSAGetLastError();
   } else {
      mConnected = true;   
   }
}

// -------------------------------------------------------------
// Constructor for connection to a hostname or IP address
// -------------------------------------------------------------

ClientSocket::ClientSocket(string HostnameOrIPAddress, ushort remoteport)
{
   // Need to create either a 32-bit or 64-bit socket handle
   mConnected = false;
   mLastWSAError = 0;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      uint proto = IPPROTO_TCP;
      mSocket64 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket64 == INVALID_SOCKET64) {
         mLastWSAError = WSAGetLastError();
         return;
      }
   } else {
      int proto = IPPROTO_TCP;
      mSocket32 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket32 == INVALID_SOCKET32) {
         mLastWSAError = WSAGetLastError();
         return;
      }
   }

   // Is it an IP address?
   uchar arrName[];
   StringToCharArray(HostnameOrIPAddress, arrName);
   ArrayResize(arrName, ArraySize(arrName) + 1);
   uint addr = inet_addr(arrName);
   if (addr == INADDR_NONE) {
      // Not an IP address. Need to look up the name
      // .......................................................................................
      // Unbelievably horrible handling of the hostent structure depending on whether
      // we're in 32-bit or 64-bit, with different-length memory pointers...
      if (TerminalInfoInteger(TERMINAL_X64)) {
         char arrName64[];
         ArrayResize(arrName64, ArraySize(arrName));
         for (int i = 0; i < ArraySize(arrName); i++) arrName64[i] = (char)arrName[i];
         ulong nres = gethostbyname(arrName64);
         if (nres == 0) {
            // Name lookup failed
            return;
         } else {
            // Need to navigate the hostent structure. Very, very ugly...
            ushort addrlen;
            RtlMoveMemory(addrlen, nres + 18, 2);
            if (addrlen == 0) {
               // No addresses associated with name
               return;
            } else {
               ulong ptr1, ptr2, ptr3;
               RtlMoveMemory(ptr1, nres + 24, 8);
               RtlMoveMemory(ptr2, ptr1, 8);
               RtlMoveMemory(ptr3, ptr2, 4);
               addr = (uint)ptr3;
            }
         }
      } else {
         uint nres = gethostbyname(arrName);
         if (nres == 0) {
            // Name lookup failed
            return;
         } else {
            // Need to navigate the hostent structure. Very, very ugly...
            ushort addrlen;
            RtlMoveMemory(addrlen, nres + 10, 2);
            if (addrlen == 0) {
               // No addresses associated with name
               return;
            } else {
               int ptr1, ptr2;
               RtlMoveMemory(ptr1, nres + 12, 4);
               RtlMoveMemory(ptr2, ptr1, 4);
               RtlMoveMemory(addr, ptr2, 4);
            }
         }
      }
   } else {
      // The HostnameOrIPAddress parameter is an IP address
   }

   // Fill in the address and port into a sockaddr_in structure
   sockaddr server;
   server.family = AF_INET;
   server.port = htons(remoteport);
   server.address = addr;

   // connect() call has to differ between 32-bit and 64-bit
   int res;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      res = connect(mSocket64, server, sizeof(sockaddr));
   } else {
      res = connect(mSocket32, server, sizeof(sockaddr));
   }
   if (res == SOCKET_ERROR) {
      // Oops
      mLastWSAError = WSAGetLastError();
   } else {
      mConnected = true;   
   }
}

// -------------------------------------------------------------
// Constructor for client sockets from server sockets
// -------------------------------------------------------------

ClientSocket::ClientSocket(SOCKET_HANDLE32 clientsocket32)
{
   // Need to create either a 32-bit or 64-bit socket handle
   mConnected = true;
   mSocket32 = clientsocket32;
}

ClientSocket::ClientSocket(SOCKET_HANDLE64 clientsocket64)
{
   // Need to create either a 32-bit or 64-bit socket handle
   mConnected = true;
   mSocket64 = clientsocket64;
}


// -------------------------------------------------------------
// Destructor. Close the socket if created
// -------------------------------------------------------------

ClientSocket::~ClientSocket()
{
   if (TerminalInfoInteger(TERMINAL_X64)) {
      if (mSocket64 != 0)  closesocket(mSocket64);
   } else {
      if (mSocket32 != 0)  closesocket(mSocket32);
   }   
}

// -------------------------------------------------------------
// Simple send function
// -------------------------------------------------------------

bool ClientSocket::Send(string strMsg)
{
   if (!mConnected) return false;
   
   bool bRetval = true;
   uchar arr[];
   StringToCharArray(strMsg, arr);
   int szToSend = StringLen(strMsg);
   
   while (szToSend > 0) {
      int res;
      if (TerminalInfoInteger(TERMINAL_X64)) {
         res = send(mSocket64, arr, szToSend, 0);
      } else {
         res = send(mSocket32, arr, szToSend, 0);
      }
      
      if (res == SOCKET_ERROR || res == 0) {
         szToSend = -1;
         bRetval = false;
         mConnected = false;
      } else {
         szToSend -= res;
         if (szToSend > 0) ArrayCopy(arr, arr, 0, res, szToSend);
      }
   }

   return bRetval;
}

bool ClientSocket::SendRaw(uchar &buffer[], const int buffer_size)
{
   if (!mConnected) return false;
   
   bool bRetval = true;
   uchar arr[];
   ArrayResize(arr, buffer_size);
   ArrayCopy(arr, buffer, 0, 0, buffer_size);
   int szToSend = buffer_size;
   
   while (szToSend > 0) {
      int res;
      if (TerminalInfoInteger(TERMINAL_X64)) {
         res = send(mSocket64, arr, szToSend, 0);
      } else {
         res = send(mSocket32, arr, szToSend, 0);
      }
      
      if (res == SOCKET_ERROR || res == 0) {
         szToSend = -1;
         bRetval = false;
         mConnected = false;
      } else {
         szToSend -= res;
         if (szToSend > 0) ArrayCopy(arr, arr, 0, res, szToSend);
      }
   }

   return bRetval;
}

// -------------------------------------------------------------
// Simple receive function. Without a message separator,
// it simply returns all the data sitting on the socket.
// With a separator, it stores up incoming data until
// it sees the separator, and then returns the text minus
// the separator.
// Returns a blank string once no (more) data is waiting
// for collection.
// -------------------------------------------------------------

string ClientSocket::Receive(string MessageSeparator = "")
{
   if (!mConnected) return "";
   
   string strRetval = "";
   
   uchar arrBuffer[];
   int BufferSize = 10000;
   ArrayResize(arrBuffer, BufferSize);

   uint nonblock = 1;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      ioctlsocket(mSocket64, FIONBIO, nonblock);
  
      int res = 1;
      while (res > 0) {
         res = recv(mSocket64, arrBuffer, BufferSize, 0);
         if (res > 0) {
            StringAdd(mPendingReceiveData, CharArrayToString(arrBuffer, 0, res));
         } else {
            if (WSAGetLastError() != WSAWOULDBLOCK) mConnected = false;
         }
      }
   } else {
      ioctlsocket(mSocket32, FIONBIO, nonblock);

      int res = 1;
      while (res > 0) {
         res = recv(mSocket32, arrBuffer, BufferSize, 0);
         if (res > 0) {
            StringAdd(mPendingReceiveData, CharArrayToString(arrBuffer, 0, res));
         } else {
            if (WSAGetLastError() != WSAWOULDBLOCK) mConnected = false;
         }
      }
   }   
   
   if (mPendingReceiveData == "") {
      // No data
      
   } else if (MessageSeparator == "") {
      // No requested message separator to wait for
      strRetval = mPendingReceiveData;
      mPendingReceiveData = "";
   
   } else {
      int idx = StringFind(mPendingReceiveData, MessageSeparator);
      if (idx >= 0) {
         while (idx == 0) {
            mPendingReceiveData = StringSubstr(mPendingReceiveData, idx + StringLen(MessageSeparator));
            idx = StringFind(mPendingReceiveData, MessageSeparator);
         }
         
         strRetval = StringSubstr(mPendingReceiveData, 0, idx);
         mPendingReceiveData = StringSubstr(mPendingReceiveData, idx + StringLen(MessageSeparator));
      }
   }
   
   return strRetval;
}

// -------------------------------------------------------------
// Server socket class
// -------------------------------------------------------------

class ServerSocket
{
   private:
      SOCKET_HANDLE32 mSocket32;
      SOCKET_HANDLE64 mSocket64;

      // Other state variables
      bool mCreated;
      int mLastWSAError;
              
   public:
      ServerSocket(ushort ServerPort, bool ForLocalhostOnly);
      ~ServerSocket();
      
      ClientSocket * Accept();

      bool Created() {return mCreated;}
      int GetLastSocketError() {return mLastWSAError;}
};


// -------------------------------------------------------------
// Constructor for server socket
// -------------------------------------------------------------

ServerSocket::ServerSocket(ushort ServerPort, bool ForLocalhostOnly)
{
   // Create socket and make it non-blocking
   mCreated = false;
   mLastWSAError = 0;
   if (TerminalInfoInteger(TERMINAL_X64)) {
      uint proto = IPPROTO_TCP;
      mSocket64 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket64 == INVALID_SOCKET64) {
         mLastWSAError = WSAGetLastError();
         return;
      }
      uint nonblock = 1;
      ioctlsocket(mSocket64, FIONBIO, nonblock);

   } else {
      int proto = IPPROTO_TCP;
      mSocket32 = socket(AF_INET, SOCK_STREAM, proto);
      if (mSocket32 == INVALID_SOCKET32) {
         mLastWSAError = WSAGetLastError();
         return;
      }
      uint nonblock = 1;
      ioctlsocket(mSocket32, FIONBIO, nonblock);
   }

   // Try a bind
   sockaddr server;
   server.family = AF_INET;
   server.port = htons(ServerPort);
   server.address = (ForLocalhostOnly ? 0x100007f : 0); // 127.0.0.1 or INADDR_ANY

   if (TerminalInfoInteger(TERMINAL_X64)) {
      int bindres = bind(mSocket64, server, sizeof(sockaddr));
      if (bindres != 0) {
         // Bind failed
      } else {
         int listenres = listen(mSocket64, 10);
         if (listenres != 0) {
            // Listen failed
         } else {
            mCreated = true;         
         }
      }
   } else {
      int bindres = bind(mSocket32, server, sizeof(sockaddr));
      if (bindres != 0) {
         // Bind failed
      } else {
         int listenres = listen(mSocket32, 10);
         if (listenres != 0) {
            // Listen failed
         } else {
            mCreated = true;         
         }
      }
   }
}


// -------------------------------------------------------------
// Destructor. Close the socket if created
// -------------------------------------------------------------

ServerSocket::~ServerSocket()
{
   if (TerminalInfoInteger(TERMINAL_X64)) {
      if (mSocket64 != 0)  closesocket(mSocket64);
   } else {
      if (mSocket32 != 0)  closesocket(mSocket32);
   }   
}

// -------------------------------------------------------------
// Accepts any incoming connection. Returns either NULL,
// or an instance of ClientSocket
// -------------------------------------------------------------

ClientSocket * ServerSocket::Accept()
{
   if (!mCreated) return NULL;
   
   ClientSocket * pClient = NULL;

   if (TerminalInfoInteger(TERMINAL_X64)) {
      SOCKET_HANDLE64 acc = accept(mSocket64, 0, 0);
      if (acc != INVALID_SOCKET64) {
         pClient = new ClientSocket(acc);
      }
   } else {
      SOCKET_HANDLE32 acc = accept(mSocket32, 0, 0);
      if (acc != INVALID_SOCKET32) {
         pClient = new ClientSocket(acc);
      }
   }

   return pClient;
}
