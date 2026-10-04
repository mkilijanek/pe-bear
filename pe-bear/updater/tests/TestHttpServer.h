#pragma once

#include <QtCore>
#include <QtNetwork>

/**
 * Minimal single-connection HTTP/1.1 server for the download tests.
 *
 * Serves one canned body, optionally lying about Content-Length or cutting the
 * connection part-way, so the download path can be exercised against a real
 * socket instead of a mock.
 */
class TestHttpServer : public QTcpServer
{
	Q_OBJECT

public:
	enum Behaviour {
		ServeWholeBody = 0,
		/** Announce the declared length, send less, then close. */
		TruncateAndClose,
		/** Answer with an HTTP error status. */
		RespondNotFound,
		/** Accept the connection and then never answer. */
		StaySilent
	};

	explicit TestHttpServer(QObject *parent = NULL)
		: QTcpServer(parent), m_behaviour(ServeWholeBody), m_declaredLength(-1)
	{
		connect(this, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
	}

	void setBody(const QByteArray &body) { m_body = body; }
	void setBehaviour(Behaviour b) { m_behaviour = b; }
	/** Content-Length to announce; -1 means "the real body length". */
	void setDeclaredLength(qint64 length) { m_declaredLength = length; }

	QUrl urlFor(const QString &path) const
	{
		return QUrl(QLatin1String("http://127.0.0.1:") + QString::number(serverPort()) + path);
	}

private slots:
	void onNewConnection()
	{
		QTcpSocket *socket = nextPendingConnection();
		if (!socket) return;
		connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
		connect(socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
	}

	void onReadyRead()
	{
		QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
		if (!socket) return;
		const QByteArray request = socket->readAll();
		if (!request.contains("\r\n\r\n")) return;

		if (m_behaviour == StaySilent) {
			return;
		}
		if (m_behaviour == RespondNotFound) {
			socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
			socket->flush();
			socket->disconnectFromHost();
			return;
		}

		const qint64 declared = (m_declaredLength >= 0) ? m_declaredLength : m_body.size();
		QByteArray header = "HTTP/1.1 200 OK\r\n";
		header += "Content-Type: application/octet-stream\r\n";
		header += "Content-Length: " + QByteArray::number(declared) + "\r\n";
		header += "Connection: close\r\n\r\n";
		socket->write(header);

		QByteArray body = m_body;
		if (m_behaviour == TruncateAndClose && body.size() > 1) {
			body.truncate(body.size() / 2);
		}
		socket->write(body);
		socket->flush();
		socket->disconnectFromHost();
	}

private:
	QByteArray m_body;
	Behaviour m_behaviour;
	qint64 m_declaredLength;
};
