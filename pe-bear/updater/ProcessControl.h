#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * A specific running process, as opposed to a number that currently names one.
 *
 * The distinction matters here. The helper waits for PE-bear to exit before it
 * touches the installation, and a bare pid is not enough to wait on: the
 * operating system is free to give that number to something else the moment
 * the process is gone. A helper that only asked "does pid 4242 exist?" could
 * be told "yes" by an unrelated program and wait out its timeout for nothing.
 *
 * So the identity carries a platform token alongside the pid, captured while
 * the process is still alive, and both have to match for it to count as the
 * same process.
 */
struct ProcessIdentity
{
	ProcessIdentity() : pid(0) {}

	qint64 pid;
	/**
	 * Distinguishes this process from a later one with the same pid: its
	 * start time on Linux, its creation time on Windows. Empty where the
	 * platform cannot supply one.
	 */
	QString startToken;

	bool isValid() const { return pid > 0; }
};

//----------------------------------------------------------------------

/**
 * Asks the operating system whether a process is still running.
 *
 * An interface so that the helper's waiting logic -- including the timeout,
 * which is the part with teeth -- can be driven without starting or killing
 * anything. Nothing here can terminate a process, and that is deliberate: the
 * helper must never force PE-bear to quit, because unsaved work is the user's,
 * not the updater's to discard.
 */
class IProcessProbe
{
public:
	virtual ~IProcessProbe() {}

	/**
	 * Captures the identity of @p pid. Returns an invalid identity when the
	 * process is already gone, which is not an error -- it is the condition
	 * the helper is waiting for.
	 */
	virtual ProcessIdentity identify(qint64 pid) = 0;

	/** Whether @p who is still the live process it was when identified. */
	virtual bool isRunning(const ProcessIdentity &who) const = 0;

	/** Blocks for @p ms. Behind the interface so tests do not actually wait. */
	virtual void sleep(int ms) = 0;

	/** Milliseconds since an arbitrary fixed point; monotonic. */
	virtual qint64 elapsedMs() const = 0;

	virtual QString lastError() const = 0;
};

//----------------------------------------------------------------------

/** Starts other programs. Separate from the probe: different risk, same OS. */
class IProcessLauncher
{
public:
	virtual ~IProcessLauncher() {}

	struct Result
	{
		Result() : started(false), exited(false), exitCode(-1), crashed(false) {}

		bool started;
		/** False when the timeout elapsed first; the process was left alone. */
		bool exited;
		int exitCode;
		/** The process died on a signal or an unhandled exception. */
		bool crashed;

		bool exitedCleanly() const { return started && exited && !crashed; }
	};

	/** Runs @p exe and waits up to @p timeoutMs for it to exit. */
	virtual Result runAndWait(const QString &exe, const QStringList &args,
		const QString &workingDir, int timeoutMs) = 0;

	/** Starts @p exe and does not wait; it outlives this process. */
	virtual bool startDetached(const QString &exe, const QStringList &args,
		const QString &workingDir) = 0;

	virtual QString lastError() const = 0;
};

//----------------------------------------------------------------------

/**
 * The real thing.
 *
 * The per-platform differences are only in how a process is identified, and
 * each is chosen so that being wrong fails safe -- towards "still running",
 * which makes the helper refuse and change nothing:
 *
 *  - Windows: a handle opened while the process is alive. Holding it keeps the
 *    kernel object alive, so the pid cannot be handed to anything else while
 *    the helper is waiting. This is the strongest of the three, which is
 *    fortunate, because Windows is where it matters.
 *  - Linux: the start time from /proc/<pid>/stat, which the kernel reports in
 *    clock ticks since boot. A reused pid gets a different one.
 *  - Other POSIX, macOS included: existence only, via kill(pid, 0). A reused
 *    pid therefore reads as "still running" and the helper refuses rather than
 *    proceeding -- the safe direction, at the cost of a pointless refusal in a
 *    case that needs the pid to be recycled within a few seconds.
 */
class RealProcessProbe : public IProcessProbe
{
public:
	RealProcessProbe();
	virtual ~RealProcessProbe();

	virtual ProcessIdentity identify(qint64 pid);
	virtual bool isRunning(const ProcessIdentity &who) const;
	virtual void sleep(int ms);
	virtual qint64 elapsedMs() const;
	virtual QString lastError() const { return m_lastError; }

private:
	/** Platform token for @p pid, or empty where there is none. */
	QString tokenFor(qint64 pid) const;

	QElapsedTimer m_clock;
	mutable QString m_lastError;

#if defined(Q_OS_WIN)
	/**
	 * Held open for the lifetime of the probe. One subject only -- the helper
	 * waits for exactly one process, and a probe that silently tracked several
	 * would leak handles for the ones it stopped caring about.
	 */
	void *m_handle;
	qint64 m_handlePid;
	void closeHandle();
#endif
};

//----------------------------------------------------------------------

class RealProcessLauncher : public IProcessLauncher
{
public:
	/**
	 * Share of @p totalMs allowed for the process to start, the rest being
	 * left for it to finish. Exposed so the arithmetic can be checked without
	 * starting anything; the contract is that the two together never exceed
	 * the caller's budget.
	 */
	static int startBudgetMs(int totalMs);

	virtual Result runAndWait(const QString &exe, const QStringList &args,
		const QString &workingDir, int timeoutMs);
	virtual bool startDetached(const QString &exe, const QStringList &args,
		const QString &workingDir);
	virtual QString lastError() const { return m_lastError; }

private:
	QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear
