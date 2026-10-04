#include "Transaction.h"

using namespace pe_bear::updater;

QString Transaction::recoveryActionToString(RecoveryAction a)
{
	switch (a) {
		case RecoveryNone: return QLatin1String("None");
		case RecoveryRollBack: return QLatin1String("RollBack");
		case RecoveryFinishCommit: return QLatin1String("FinishCommit");
		case RecoveryManual: return QLatin1String("Manual");
		default: return QLatin1String("Invalid");
	}
}

Transaction::RecoveryAction Transaction::planRecovery(const TransactionRecord &record)
{
	/* An incomplete record cannot be acted on safely: it may describe a
	   half-replaced installation whose details are missing. */
	if (!record.isValid()) return RecoveryManual;

	switch (record.state) {
		case TxPrepared:
			/* Nothing irreversible happened yet. Any recorded step is still
			   undone for tidiness, but there is no installation at risk. */
			return record.ops.isEmpty() ? RecoveryNone : RecoveryRollBack;

		case TxBackedUp:
		case TxActivated:
			/* The installation is mid-replacement. Undo. */
			return RecoveryRollBack;

		case TxValidated:
			/* The new build ran and confirmed itself; only the bookkeeping and
			   the backup removal are outstanding. Rolling back here would undo
			   a working installation, which is the wrong direction. */
			return RecoveryFinishCommit;

		case TxCommitted:
		case TxRolledBack:
			return RecoveryNone;

		case TxFailed:
			/* Already given up once; a second automatic attempt would most
			   likely fail the same way and obscure the original reason. */
			return RecoveryManual;

		default:
			return RecoveryManual;
	}
}

//----------------------------------------------------------------------

Transaction::Transaction(IFileSystem *fs, TransactionJournal *journal)
	: m_fs(fs), m_journal(journal)
{
}

bool Transaction::refuse(const QString &why)
{
	m_lastError = why;
	return false;
}

bool Transaction::setState(TransactionState next)
{
	const TransactionState previous = m_record.state;
	m_record.state = next;
	if (!m_journal->write(m_record)) {
		m_record.state = previous;
		return refuse(QLatin1String("could not record state ")
			+ transactionStateToString(next) + QLatin1String(": ") + m_journal->lastError());
	}
	return true;
}

bool Transaction::begin(const TransactionRecord &seed, const QString &id)
{
	if (!m_fs || !m_journal) return refuse(QLatin1String("not wired up"));

	m_record = seed;
	m_record.id = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
	m_record.state = TxPrepared;
	m_record.ops.clear();
	m_record.error.clear();
	m_record.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	if (!m_record.isValid()) {
		return refuse(QLatin1String("the attempt is missing target, package or digest"));
	}
	if (!m_journal->prepare()) {
		return refuse(QLatin1String("journal unavailable: ") + m_journal->lastError());
	}
	/* Written before anything is touched, so an interruption during the very
	   first step still leaves a record behind. */
	if (!m_journal->write(m_record)) {
		return refuse(QLatin1String("could not open the journal: ") + m_journal->lastError());
	}
	return true;
}

bool Transaction::load(const QString &id)
{
	if (!m_journal) return refuse(QLatin1String("not wired up"));
	bool ok = false;
	const TransactionRecord rec = m_journal->read(id, &ok);
	if (!ok) return refuse(m_journal->lastError());
	m_record = rec;
	return true;
}

bool Transaction::backup(const QString &backupDir)
{
	if (m_record.state != TxPrepared) {
		return refuse(QLatin1String("backup requires Prepared, not ")
			+ transactionStateToString(m_record.state));
	}
	if (backupDir.isEmpty()) return refuse(QLatin1String("no backup directory"));
	if (m_fs->exists(backupDir)) {
		return refuse(QLatin1String("the backup directory already exists"));
	}
	if (!m_fs->exists(m_record.targetDir)) {
		return refuse(QLatin1String("the installation to back up does not exist"));
	}

	/* Recorded before the move: a move that happened without a record is
	   exactly the damage this class exists to prevent. */
	m_record.backupDir = backupDir;
	m_record.ops.append(TransactionOp(TransactionOp::OpMoved, m_record.targetDir, backupDir));
	if (!m_journal->write(m_record)) {
		m_record.ops.removeLast();
		return refuse(QLatin1String("could not record the backup step: ") + m_journal->lastError());
	}

	if (!m_fs->movePath(m_record.targetDir, backupDir)) {
		/* The step is left in the journal on purpose. Undoing it is a no-op if
		   the move never happened, and the alternative -- removing the record
		   and finding the move did happen -- is unrecoverable. */
		return refuse(QLatin1String("could not move the installation aside: ") + m_fs->lastError());
	}
	return setState(TxBackedUp);
}

bool Transaction::markActivated(const QList<TransactionOp> &activationOps)
{
	if (m_record.state != TxBackedUp) {
		return refuse(QLatin1String("activation requires BackedUp, not ")
			+ transactionStateToString(m_record.state));
	}
	QList<TransactionOp>::const_iterator itr;
	for (itr = activationOps.begin(); itr != activationOps.end(); ++itr) {
		if (!itr->isValid()) return refuse(QLatin1String("an activation step is incomplete"));
	}
	const int before = m_record.ops.size();
	for (itr = activationOps.begin(); itr != activationOps.end(); ++itr) {
		m_record.ops.append(*itr);
	}
	m_record.state = TxActivated;
	if (!m_journal->write(m_record)) {
		while (m_record.ops.size() > before) m_record.ops.removeLast();
		m_record.state = TxBackedUp;
		return refuse(QLatin1String("could not record activation: ") + m_journal->lastError());
	}
	return true;
}

bool Transaction::markValidated()
{
	if (m_record.state != TxActivated) {
		return refuse(QLatin1String("validation requires Activated, not ")
			+ transactionStateToString(m_record.state));
	}
	return setState(TxValidated);
}

bool Transaction::commit()
{
	if (m_record.state != TxValidated) {
		return refuse(QLatin1String("commit requires Validated, not ")
			+ transactionStateToString(m_record.state));
	}
	/* The state goes first. If removing the backup fails afterwards the
	   installation is still correct and the leftover is only wasted space;
	   doing it the other way round could leave a Validated record pointing at
	   a backup that is already gone. */
	if (!setState(TxCommitted)) return false;

	if (!m_record.backupDir.isEmpty() && m_fs->exists(m_record.backupDir)) {
		if (!m_fs->removeDirRecursively(m_record.backupDir)) {
			/* Not a failure of the installation. Recorded and reported. */
			m_record.error = QLatin1String("installed, but the backup could not be removed: ")
				+ m_fs->lastError();
			m_journal->write(m_record);
		}
	}
	return true;
}

bool Transaction::undoOp(const TransactionOp &op, QString *error)
{
	switch (op.kind) {
		case TransactionOp::OpMoved:
			/* Undone only if it actually happened; absent source means the
			   move was recorded but interrupted before it ran. */
			if (!m_fs->exists(op.to)) return true;
			if (m_fs->exists(op.from)) {
				if (error) *error = QLatin1String("cannot restore ")
					+ QDir::toNativeSeparators(op.from) + QLatin1String(": already present");
				return false;
			}
			if (!m_fs->movePath(op.to, op.from)) {
				if (error) *error = m_fs->lastError();
				return false;
			}
			return true;

		case TransactionOp::OpCreated:
			if (!m_fs->exists(op.from)) return true;
			if (!m_fs->removeFile(op.from)) {
				if (error) *error = m_fs->lastError();
				return false;
			}
			return true;

		case TransactionOp::OpCreatedDir:
			if (!m_fs->exists(op.from)) return true;
			if (!m_fs->removeDirRecursively(op.from)) {
				if (error) *error = m_fs->lastError();
				return false;
			}
			return true;

		default:
			if (error) *error = QLatin1String("unknown step kind");
			return false;
	}
}

bool Transaction::rollBack(const QString &reason)
{
	if (isTerminalState(m_record.state)) {
		return refuse(QLatin1String("cannot roll back from ")
			+ transactionStateToString(m_record.state));
	}

	QStringList problems;
	/* Reverse order: a later step may depend on an earlier one, so undoing
	   forwards could try to restore something still buried. */
	for (int i = m_record.ops.size() - 1; i >= 0; i--) {
		QString error;
		if (!undoOp(m_record.ops.at(i), &error)) {
			problems << (transactionOpKindToString(m_record.ops.at(i).kind)
				+ QLatin1String(" ") + QDir::toNativeSeparators(m_record.ops.at(i).from)
				+ QLatin1String(": ") + error);
			/* Deliberately continuing. Stopping here would leave strictly more
			   of the installation in pieces than attempting the rest. */
		}
	}

	if (problems.isEmpty()) {
		m_record.error = reason;
		return setState(TxRolledBack);
	}
	m_record.error = reason + QLatin1String(" | rollback incomplete: ")
		+ problems.join(QLatin1String("; "));
	setState(TxFailed);
	return refuse(m_record.error);
}

bool Transaction::fail(const QString &reason)
{
	m_record.error = reason;
	if (!setState(TxFailed)) return false;
	return true;
}
