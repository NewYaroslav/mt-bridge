// *******************************************************************************
// Very basic client-socket library, currently with support only for 
// sending data, not receiving it.
// *******************************************************************************
#property strict


// -------------------------------------------------------------
// WinInet constants and structures
// -------------------------------------------------------------

#define SOCKET_HANDLE         uint // Change to ulong for 64-bit MT5 (untested)
#define AF_INET               2
#define SOCK_STREAM           1
#define IPPROTO_TCP           6
#define INVALID_SOCKET        0
#define SOCKET_ERROR          -1
#define INADDR_NONE           0xFFFFFFFF

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
   SOCKET_HANDLE socket(int, int, int);
   int connect(SOCKET_HANDLE, sockaddr&, int);
   int closesocket(SOCKET_HANDLE);
   int send(SOCKET_HANDLE, uchar&[],int,int);
   uint inet_addr(uchar&[]);
   uint gethostbyname(uchar&[]);
   int WSAGetLastError();
   uint htonl(uint);
   ushort htons(ushort);
#import

#import "kernel32.dll"
   void RtlMoveMemory(uint&, uint, int);
   void RtlMoveMemory(ushort&, uint, int);
#import


// -------------------------------------------------------------
// Class definition
// -------------------------------------------------------------

class ClientSocket
{
   private:
      SOCKET_HANDLE mSocket;
      bool mConnected;
      int mLastWSAError;
              
   public:
      ClientSocket(ushort localport);
      ClientSocket(string HostnameOrIPAddress, ushort localport);
      ~ClientSocket();
      bool Send(string strMsg);
      bool SendRaw(uchar &buffer[], const int buffer_size);
      bool IsSocketConnected() {return mConnected;}
      int GetLastSocketError() {return mLastWSAError;}
};


// -------------------------------------------------------------
// Constructor a simple connection to 127.0.0.1
// -------------------------------------------------------------

ClientSocket::ClientSocket(ushort localport)
{
   mConnected = false;
   mLastWSAError = 0;
   mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (mSocket == INVALID_SOCKET) {
      // Ooops
      mLastWSAError = WSAGetLastError();
   } else {
      sockaddr server;
      server.family = AF_INET;
      server.port = htons(localport);
      server.address = 0x100007f; // 127.0.0.1
      int res = connect(mSocket, server, sizeof(sockaddr));
      if (res == SOCKET_ERROR) {
         // Oops
         mLastWSAError = WSAGetLastError();
      } else {
         mConnected = true;   
      }
   }
}

// -------------------------------------------------------------
// Constructor for connection to a hostname or IP address
// -------------------------------------------------------------

ClientSocket::ClientSocket(string HostnameOrIPAddress, ushort remoteport)
{
   mConnected = false;
   mLastWSAError = 0;
   mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (mSocket == INVALID_SOCKET) {
      // Ooops
      mLastWSAError = WSAGetLastError();
   } else {
      // Is it an IP address?
      uchar arrName[];
      StringToCharArray(HostnameOrIPAddress, arrName);
      ArrayResize(arrName, ArraySize(arrName) + 1);
      uint addr = inet_addr(arrName);
      if (addr == INADDR_NONE) {
         // Not an IP address. Need to look up the name
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
      } else {
         // Is an IP address
      }
      
      sockaddr server;
      server.family = AF_INET;
      server.port = htons(remoteport);
      server.address = addr;
      int res = connect(mSocket, server, sizeof(sockaddr));
      if (res == SOCKET_ERROR) {
         // Oops
         mLastWSAError = WSAGetLastError();
      } else {
         mConnected = true;   
      }
   }
}

// -------------------------------------------------------------
// Destructor. Close the socket if connected
// -------------------------------------------------------------

ClientSocket::~ClientSocket()
{
   if (mSocket != 0) {
      closesocket(mSocket);
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
      int res = send(mSocket, arr, szToSend, 0);
      if (res == SOCKET_ERROR || res == 0) {
         szToSend = -1;
         bRetval = false;
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
   ArrayResize(arr,buffer_size);
   ArrayCopy(arr, buffer, 0, 0, buffer_size);
   int szToSend = buffer_size;
   
   while (szToSend > 0) {
      int res = send(mSocket, arr, szToSend, 0);
      if (res == SOCKET_ERROR || res == 0) {
         szToSend = -1;
         bRetval = false;
      } else {
         szToSend -= res;
         if (szToSend > 0) ArrayCopy(arr, arr, 0, res, szToSend);
      }
   }

   return bRetval;
}