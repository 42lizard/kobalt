#pragma once

#ifdef KOBALT_LOCAL_RESOURCE_SOCKET
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#else
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#endif

namespace kobalt {
#ifdef KOBALT_LOCAL_RESOURCE_SOCKET
using ResourceSocket = QLocalSocket;
using ResourceListener = QLocalServer;

// The endpoint number identifies this opening within the process, not a TCP port.
inline QString resourceSocketName(int endpoint) {
    return QStringLiteral("kobalt-resources-%1-%2").arg(QCoreApplication::applicationPid()).arg(endpoint);
}
inline void connectResourceSocket(ResourceSocket &socket, int endpoint) {
    socket.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
    socket.connectToServer(resourceSocketName(endpoint));
}
inline void disconnectResourceSocket(ResourceSocket *socket) {
    socket->disconnectFromServer();
}
#else
using ResourceSocket = QTcpSocket;
using ResourceListener = QTcpServer;

inline void connectResourceSocket(ResourceSocket &socket, int port) {
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(QHostAddress::LocalHost, port);
}
inline void disconnectResourceSocket(ResourceSocket *socket) {
    socket->disconnectFromHost();
}
#endif
}
