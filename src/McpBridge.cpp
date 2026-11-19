#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <iostream>
#include <string>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const auto args = app.arguments();
    auto value = [&](QString key) {
        int i = args.indexOf(key);
        return i >= 0 ? args.value(i + 1) : QString();
    };
    const auto pipe = value("--pipe"), token = value("--token");
    if (pipe.isEmpty() || token.isEmpty()) {
        std::cerr << "Use the MCP client configuration copied from Worldbench's Automation menu.\n";
        return 2;
    }
    QLocalSocket socket;
    socket.connectToServer(pipe);
    if (!socket.waitForConnected(5000)) {
        std::cerr << "Worldbench session unavailable. Enable MCP and copy a fresh configuration.\n";
        return 3;
    }
    // stdio transport: one UTF-8 JSON-RPC message per line; stdout contains protocol only.
    std::string line;
    QByteArray pending;
    while (true) {
        line.clear();
        char c;
        while (std::cin.get(c) && c != '\n') {
            if (line.size() >= 8 * 1024 * 1024) {
                std::cerr << "Message exceeds 8 MiB\n";
                return 4;
            }
            line.push_back(c);
        }
        if (line.empty() && !std::cin)
            break;
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(line), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cout << "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse "
                         "error\"}}\n"
                      << std::flush;
            continue;
        }
        auto request = doc.object();
        QJsonObject envelope{{"token", token}, {"request", request}};
        socket.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact) + '\n');
        while (socket.bytesToWrite() > 0)
            if (!socket.waitForBytesWritten(5000))
                return 5;
        if (!request.contains("id"))
            continue;
        while (!pending.contains('\n')) {
            if (!socket.bytesAvailable() && !socket.waitForReadyRead(60000)) {
                std::cerr << "Editor disconnected or timed out\n";
                return 6;
            }
            pending += socket.readAll();
            if (pending.size() > 16 * 1024 * 1024)
                return 7;
        }
        int end = pending.indexOf('\n');
        std::cout << pending.left(end).constData() << '\n' << std::flush;
        pending.remove(0, end + 1);
    }
    return 0;
}
