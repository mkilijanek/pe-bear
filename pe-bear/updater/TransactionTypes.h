#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * Where an installation has got to.
 *
 * The order matters: every state above Committed implies the one before it
 * succeeded, which is what lets recovery decide what to do from the journal
 * alone. The three terminal states are deliberately distinct -- "rolled back"
 * and "failed" are not the same thing to someone reading a log after the fact.
 */
enum TransactionState {
	/* nothing has been touched yet; the package is verified and staged */
	TxPrepared = 0,
	/* the existing installation has been copied aside */
	TxBackedUp,
	/* the new files are in place; the old ones are still in the backup */
	TxActivated,
	/* the new build started and confirmed itself */
	TxValidated,
	/* finished; the backup may be discarded */
	TxCommitted,
	/* the previous installation was restored */
	TxRolledBack,
	/* gave up, and rollback did not complete either -- needs a human */
	TxFailed,
	TRANSACTION_STATES_COUNT
};

QString transactionStateToString(TransactionState s);
TransactionState transactionStateFromString(const QString &s, bool *ok = NULL);

/** True for a state from which no further automatic progress is possible. */
bool isTerminalState(TransactionState s);

//----------------------------------------------------------------------

/**
 * One reversible step, recorded as it happens.
 *
 * A rollback replays these in reverse, so the record has to carry enough to
 * undo the step without consulting anything else -- after an interruption
 * there may be nothing else left to consult.
 */
struct TransactionOp
{
	enum Kind {
		OpNone = 0,
		/** A path was moved; undo moves it back. */
		OpMoved,
		/** A path was created; undo removes it. */
		OpCreated,
		/** A directory was created; undo removes the tree. */
		OpCreatedDir,
		OP_KINDS_COUNT
	};

	TransactionOp() : kind(OpNone) {}
	TransactionOp(Kind k, const QString &f, const QString &t = QString())
		: kind(k), from(f), to(t) {}

	Kind kind;
	QString from;
	QString to;

	bool isValid() const { return kind != OpNone && !from.isEmpty(); }

	QJsonObject toJson() const;
	static TransactionOp fromJson(const QJsonObject &o, bool *ok = NULL);
};

QString transactionOpKindToString(TransactionOp::Kind k);

//----------------------------------------------------------------------

/**
 * The durable description of one installation attempt.
 *
 * Everything needed to finish or undo the work must be here, because after an
 * interruption this file is all that is left. The digest is carried so the
 * helper can re-verify the package in its own process rather than trusting
 * whoever wrote the record.
 */
struct TransactionRecord
{
	TransactionRecord() : state(TxPrepared), packageSize(0) {}

	/** Opaque per-attempt id; also the journal file name. */
	QString id;
	TransactionState state;

	/** The installation being replaced. */
	QString targetDir;
	/** Where the new files were prepared. */
	QString stagingDir;
	/** Where the previous installation was moved. */
	QString backupDir;

	/** The verified package this attempt installs. */
	QString packagePath;
	qint64 packageSize;
	/** Lowercase 64-hex SHA-256, re-checked by the helper before it acts. */
	QString packageSha256;

	/** Version being installed, for logs and for the startup handshake. */
	QString fromVersion;
	QString toVersion;

	QString createdAt;
	QString updatedAt;
	/** Why it failed, when it did. Free text, for a human. */
	QString error;

	/** Applied steps, in the order they happened. Undo walks this backwards. */
	QList<TransactionOp> ops;

	bool isValid() const;

	QByteArray toJson() const;
	static TransactionRecord fromJson(const QByteArray &data, bool *ok = NULL);
};

}; // namespace updater
}; // namespace pe_bear
