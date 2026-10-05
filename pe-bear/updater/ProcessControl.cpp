#include "ProcessControl.h"

#include <QProcess>
#include <QThread>
#include <QFile>
#include <QDir>

#if defined(Q_OS_WIN)
	#include <windows.h>
#else
	#include <signal.h>
	#include <errno.h>
#endif

namespace pe_bear {
namespace updater {

RealProcessProbe::RealProcessProbe()
#if defined(Q_OS_WIN)
	: m_handle(NULL), m_handlePid(0)
#endif
{
	m_clock.start();
}

RealProcessProbe::~RealProcessProbe()
{
#if defined(Q_OS_WIN)
	closeHandle();
#endif
}

#if defined(Q_OS_WIN)
void RealProcessProbe::closeHandle()
{
	if (m_handle) {
		CloseHandle(reinterpret_cast<HANDLE>(m_handle));
		m_handle = NULL;
	}
	m_handlePid = 0;
}
#endif

QString RealProcessProbe::tokenFor(qint64 pid) const
{
#if defined(Q_OS_WIN)
	Q_UNUSED(pid);
	/* The handle is the token on Windows; see identify(). */
	return QString();
#elif defined(Q_OS_LINUX)
	/* Field 22 of /proc/<pid>/stat is the process start time. Everything
	   before it has to be skipped by position rather than by splitting on
	   spaces, because field 2 is the executable name in parentheses and may
	   contain spaces and parentheses of its own -- splitting naively is the
	   classic way to misread this file. */
	QFile stat(QLatin1String("/proc/") + QString::number(pid) + QLatin1String("/stat"));
	if (!stat.open(QIODevice::ReadOnly)) return QString();

	const QByteArray content = stat.readAll();
	const int close = content.lastIndexOf(')');
	if (close < 0 || close + 2 >= content.size()) return QString();

	/* After ") " come fields 3 onwards; start time is the 20th of those. */
	const QList<QByteArray> fields = content.mid(close + 2).simplified().split(' ');
	const int START_TIME_INDEX = 19;
	if (fields.size() <= START_TIME_INDEX) return QString();
	return QString::fromLatin1(fields.at(START_TIME_INDEX));
#else
	Q_UNUSED(pid);
	/* No portable source for this; isRunning() falls back to existence. */
	return QString();
#endif
}

ProcessIdentity RealProcessProbe::identify(qint64 pid)
{
	ProcessIdentity who;
	if (pid <= 0) {
		m_lastError = QLatin1String("not a process id: ") + QString::number(pid);
		return who;
	}

#if defined(Q_OS_WIN)
	closeHandle();
	/* SYNCHRONIZE is all that is needed to wait on it, and asking for no more
	   than that keeps the helper working against a process it has no right to
	   inspect, let alone change. */
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
	if (!h) {
		/* Already gone, or not ours. Either way there is nothing to wait for;
		   the caller treats an invalid identity as "not running". */
		m_lastError = QLatin1String("cannot open process ") + QString::number(pid);
		return who;
	}
	m_handle = h;
	m_handlePid = pid;
	who.pid = pid;
	who.startToken = QLatin1String("handle");
	return who;
#else
	if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) {
		m_lastError = QLatin1String("no such process: ") + QString::number(pid);
		return who;
	}
	who.pid = pid;
	who.startToken = tokenFor(pid);
	return who;
#endif
}

bool RealProcessProbe::isRunning(const ProcessIdentity &who) const
{
	if (!who.isValid()) return false;

#if defined(Q_OS_WIN)
	if (!m_handle || m_handlePid != who.pid) {
		/* Asked about something this probe never identified. Reporting "still
		   running" would hang the caller on a process it cannot observe, so
		   say what is true: it is not being tracked. */
		m_lastError = QLatin1String("process ") + QString::number(who.pid)
			+ QLatin1String(" is not the one being tracked");
		return false;
	}
	return WaitForSingleObject(reinterpret_cast<HANDLE>(m_handle), 0) == WAIT_TIMEOUT;
#else
	if (::kill(static_cast<pid_t>(who.pid), 0) != 0 && errno != EPERM) return false;

	/* The pid exists. On Linux, confirm it is still the same process; a
	   different start time means the number was recycled and the process the
	   caller cares about has in fact exited. */
	if (!who.startToken.isEmpty()) {
		const QString now = tokenFor(who.pid);
		if (!now.isEmpty() && now != who.startToken) return false;
	}
	return true;
#endif
}

void RealProcessProbe::sleep(int ms)
{
	if (ms > 0) QThread::msleep(static_cast<unsigned long>(ms));
}

qint64 RealProcessProbe::elapsedMs() const
{
	return m_clock.elapsed();
}

//----------------------------------------------------------------------

IProcessLauncher::Result RealProcessLauncher::runAndWait(const QString &exe,
		const QStringList &args, const QString &workingDir, int timeoutMs)
{
	Result result;

	QProcess process;
	if (!workingDir.isEmpty()) process.setWorkingDirectory(workingDir);
	/* The child's output is not read, and an unread pipe that fills up stops
	   the process that is writing to it. Discarding is the one setting that
	   cannot deadlock the step this is used for. */
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());

	process.start(exe, args);
	if (!process.waitForStarted(timeoutMs)) {
		m_lastError = QLatin1String("could not start ") + QDir::toNativeSeparators(exe)
			+ QLatin1String(": ") + process.errorString();
		return result;
	}
	result.started = true;

	if (!process.waitForFinished(timeoutMs)) {
		/* Left running on purpose. The caller's verdict is already decided by
		   the absence of an answer, and killing a build that may be mid-write
		   adds a failure mode without changing the outcome. */
		m_lastError = QLatin1String("timed out after ") + QString::number(timeoutMs)
			+ QLatin1String(" ms waiting for ") + QDir::toNativeSeparators(exe);
		return result;
	}

	result.exited = true;
	result.crashed = (process.exitStatus() != QProcess::NormalExit);
	result.exitCode = process.exitCode();
	return result;
}

bool RealProcessLauncher::startDetached(const QString &exe, const QStringList &args,
		const QString &workingDir)
{
	/* The static overload rather than the member one: it is present in every
	   Qt version this project builds against, and detaching is the whole
	   point -- the started process has to outlive this one. */
	if (!QProcess::startDetached(exe, args, workingDir)) {
		m_lastError = QLatin1String("could not start ") + QDir::toNativeSeparators(exe);
		return false;
	}
	return true;
}

}; // namespace updater
}; // namespace pe_bear
