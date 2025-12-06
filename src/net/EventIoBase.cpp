#include <arc/net/EventIoBase.hpp>
#ifdef _WIN32
# include <WS2tcpip.h>
#else
# include <sys/socket.h>
#endif

namespace arc {

qsox::Error errorFromSocket(SockFd fd) {
    int err = 0;
    socklen_t len = sizeof(err);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) < 0) {
        return qsox::Error::lastOsError();
    }

    if (err == 0) {
        return qsox::Error::Success;
    }

    return qsox::Error::fromOs(err);
}

}