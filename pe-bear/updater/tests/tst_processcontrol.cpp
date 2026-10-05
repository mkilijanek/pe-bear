/*
 * The real process probe and launcher.
 *
 * These had no tests at all: everything using them went through fakes, which
 * is right for the orchestration but left the implementations themselves
 * unexercised. One consequence shipped -- runAndWait gave the whole of its
 * timeout to waiting for the process to start and then the whole of it again
 * to waiting for it to finish, so a caller asking for 120 seconds could be
 * held for 240.
 */
#include <QtTest>
#include "../ProcessControl.h"
#include "../Random.h"

#if defined(Q_OS_UNIX)
	#include <unistd.h>
#endif

using namespace pe_bear::updater;

namespace {

	/** A pid high enough that no running process can own it. */
	const qint64 IMPOSSIBLE_PID = 2147483646;

}; // namespace

class TestProcessControl : public QObject
{
	Q_OBJECT

private slots:
	void theStartBudgetNeverExceedsTheWhole();
	void theStartBudgetNeverExceedsTheWhole_data();
	void aProcessThatNeverFinishesIsAbandonedAtTheBudget();

	void identifiesTheRunningProcess();
	void refusesAPidThatCannotExist();
	void refusesAnIdentityItNeverIssued();
	void anInvalidIdentityIsNotRunning();
	void theClockMovesForward();

	void randomHexIsWellFormedAndUnrepeated();
	void randomHexRefusesANonPositiveLength();
};

void TestProcessControl::theStartBudgetNeverExceedsTheWhole_data()
{
	QTest::addColumn<int>("total");

	QTest::newRow("zero")        << 0;
	QTest::newRow("negative")    << -1000;
	QTest::newRow("one ms")      << 1;
	QTest::newRow("tiny")        << 40;
	QTest::newRow("one second")  << 1000;
	QTest::newRow("the default") << 120 * 1000;
	QTest::newRow("huge")        << 3600 * 1000;
}

void TestProcessControl::theStartBudgetNeverExceedsTheWhole()
{
	QFETCH(int, total);

	const int start = RealProcessLauncher::startBudgetMs(total);

	QVERIFY2(start >= 0, "a negative budget would mean an immediate give-up");
	if (total <= 0) {
		QCOMPARE(start, 0);
		return;
	}

	/* The contract: starting plus finishing is the caller's budget, not twice
	   it. This is the arithmetic half; the timing half is below. */
	QVERIFY2(start <= total, "the start slice alone exceeds the whole budget");
	/* And something is always left for the part that can legitimately be
	   slow -- unless the whole budget is a single millisecond. */
	if (total > 4) QVERIFY2(start < total, "nothing was left for the answer");
	/* Capped, so a generous overall budget does not turn into a long wait for
	   a process that was never going to start. */
	QVERIFY(start <= 10 * 1000);
}

void TestProcessControl::aProcessThatNeverFinishesIsAbandonedAtTheBudget()
{
#if !defined(Q_OS_UNIX)
	QSKIP("needs a program that starts promptly and then does not exit");
#else
	if (!QFile::exists(QLatin1String("/bin/sleep"))) {
		QSKIP("/bin/sleep is not available");
	}

	/* What this proves: a process that starts and then does not finish is
	   given up on at about the budget, and is left running rather than killed.
	
	   What it does *not* prove, stated because it would be easy to assume
	   otherwise: it does not catch the budget being spent twice. That defect
	   needs a process which is slow to *start*, and on POSIX nothing is --
	   waitForStarted returns as soon as the child is exec'd, so the start
	   slice is always near zero here whatever size it is. Tried as a mutation
	   and confirmed: restoring the doubled budget leaves this test green.
	
	   The guard against the doubling is therefore the arithmetic above, plus
	   the deadline subtraction in runAndWait. The condition that makes it
	   observable -- a first start fighting an antivirus scanner -- belongs to
	   the Windows validation. */
	RealProcessLauncher launcher;
	const int budget = 1200;

	QElapsedTimer elapsed;
	elapsed.start();
	const IProcessLauncher::Result r = launcher.runAndWait(QLatin1String("/bin/sleep"),
		QStringList() << QLatin1String("30"), QString(), budget);
	const qint64 took = elapsed.elapsed();

	QVERIFY2(r.started, qPrintable(launcher.lastError()));
	QVERIFY2(!r.exited, "sleep 30 finished within the budget?");
	QVERIFY2(took >= budget / 2, "it gave up far too early to have waited at all");
	QVERIFY2(took < 2 * budget,
		qPrintable(QLatin1String("took ") + QString::number(took)
			+ QLatin1String(" ms of a ") + QString::number(budget) + QLatin1String(" ms budget")));

	/* And the message says what it spent, which is how a person tells a
	   timeout from a crash in the log. */
	QVERIFY2(launcher.lastError().contains(QLatin1String("budget")),
		qPrintable(launcher.lastError()));
#endif
}

void TestProcessControl::identifiesTheRunningProcess()
{
	RealProcessProbe probe;

	const qint64 self = static_cast<qint64>(QCoreApplication::applicationPid());
	const ProcessIdentity me = probe.identify(self);

	QVERIFY2(me.isValid(), qPrintable(probe.lastError()));
	QCOMPARE(me.pid, self);
	QVERIFY2(probe.isRunning(me), "this process is not running, apparently");
}

void TestProcessControl::refusesAPidThatCannotExist()
{
	RealProcessProbe probe;

	const ProcessIdentity nobody = probe.identify(IMPOSSIBLE_PID);
	QVERIFY(!nobody.isValid());
	QVERIFY(!probe.lastError().isEmpty());

	/* Nor does it accept nonsense. A pid of zero or below names no process,
	   and on POSIX a signal to 0 would go to the whole process group. */
	QVERIFY(!probe.identify(0).isValid());
	QVERIFY(!probe.identify(-1).isValid());
}

void TestProcessControl::refusesAnIdentityItNeverIssued()
{
	/* Asked about something it never identified, the probe must not claim the
	   subject is alive: the caller would wait out its timeout on a process it
	   cannot observe. Saying "not running" is the honest answer, and on
	   Windows it is the only one available, since the handle is the identity. */
	RealProcessProbe probe;

	ProcessIdentity forged;
	forged.pid = IMPOSSIBLE_PID;
	forged.startToken = QLatin1String("invented");

	QVERIFY(!probe.isRunning(forged));
}

void TestProcessControl::anInvalidIdentityIsNotRunning()
{
	RealProcessProbe probe;
	QVERIFY(!probe.isRunning(ProcessIdentity()));
}

void TestProcessControl::theClockMovesForward()
{
	/* The waiting loop uses this as its deadline, so a clock that stood still
	   would turn a bounded wait into an unbounded one. */
	RealProcessProbe probe;

	const qint64 before = probe.elapsedMs();
	probe.sleep(60);
	const qint64 after = probe.elapsedMs();

	QVERIFY2(after >= before + 40, "the monotonic clock did not advance across a sleep");
	/* And a non-positive sleep is a no-op rather than an error. */
	probe.sleep(0);
	probe.sleep(-5);
}

void TestProcessControl::randomHexIsWellFormedAndUnrepeated()
{
	/* One generator for every nonce in the updater. There were two, written
	   the same way minutes apart, which is where a hardening decision stops
	   holding silently. */
	const QRegularExpression hex(QLatin1String("^[0-9a-f]+$"));

	QSet<QString> seen;
	for (int i = 0; i < 200; i++) {
		const QString value = randomHex(32);
		QCOMPARE(value.length(), 32);
		QVERIFY2(hex.match(value).hasMatch(), qPrintable(value));
		seen.insert(value);
	}
	/* Not a randomness test -- it cannot be one. It catches the mistake that
	   actually happens: a generator that returns the same thing every time. */
	QCOMPARE(seen.size(), 200);

	/* Lengths that are not multiples of the eight characters a draw yields,
	   because the padding and the truncation are both easy to get wrong. */
	const int lengths[] = { 1, 7, 8, 9, 15, 16, 31, 33, 64 };
	for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		const QString value = randomHex(lengths[i]);
		QCOMPARE(value.length(), lengths[i]);
		QVERIFY2(hex.match(value).hasMatch(), qPrintable(value));
	}
}

void TestProcessControl::randomHexRefusesANonPositiveLength()
{
	QVERIFY(randomHex(0).isEmpty());
	QVERIFY(randomHex(-1).isEmpty());
}

QTEST_GUILESS_MAIN(TestProcessControl)
#include "tst_processcontrol.moc"
