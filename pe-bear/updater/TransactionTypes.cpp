#include "TransactionTypes.h"

using namespace pe_bear::updater;

namespace {

	struct StateName { TransactionState state; const char *name; };

	/* The stored spelling is part of the on-disk format: a journal written by
	   one build is read by the next one, which may be a different version. */
	const StateName STATE_NAMES[] = {
		{ TxPrepared,   "Prepared"   },
		{ TxBackedUp,   "BackedUp"   },
		{ TxActivated,  "Activated"  },
		{ TxValidated,  "Validated"  },
		{ TxCommitted,  "Committed"  },
		{ TxRolledBack, "RolledBack" },
		{ TxFailed,     "Failed"     }
	};
	const size_t STATE_COUNT = sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]);

	struct KindName { TransactionOp::Kind kind; const char *name; };

	const KindName KIND_NAMES[] = {
		{ TransactionOp::OpMoved,      "Moved"      },
		{ TransactionOp::OpCreated,    "Created"    },
		{ TransactionOp::OpCreatedDir, "CreatedDir" }
	};
	const size_t KIND_COUNT = sizeof(KIND_NAMES) / sizeof(KIND_NAMES[0]);

}; // namespace

QString pe_bear::updater::transactionStateToString(TransactionState s)
{
	for (size_t i = 0; i < STATE_COUNT; i++) {
		if (STATE_NAMES[i].state == s) return QLatin1String(STATE_NAMES[i].name);
	}
	return QLatin1String("Invalid");
}

TransactionState pe_bear::updater::transactionStateFromString(const QString &s, bool *ok)
{
	for (size_t i = 0; i < STATE_COUNT; i++) {
		if (s == QLatin1String(STATE_NAMES[i].name)) {
			if (ok) *ok = true;
			return STATE_NAMES[i].state;
		}
	}
	/* An unreadable state must not be mistaken for Prepared, which would look
	   like "nothing has been touched" and skip a needed rollback. */
	if (ok) *ok = false;
	return TxFailed;
}

bool pe_bear::updater::isTerminalState(TransactionState s)
{
	return (s == TxCommitted || s == TxRolledBack || s == TxFailed);
}

QString pe_bear::updater::transactionOpKindToString(TransactionOp::Kind k)
{
	for (size_t i = 0; i < KIND_COUNT; i++) {
		if (KIND_NAMES[i].kind == k) return QLatin1String(KIND_NAMES[i].name);
	}
	return QLatin1String("None");
}

QJsonObject TransactionOp::toJson() const
{
	QJsonObject o;
	o.insert(QLatin1String("kind"), transactionOpKindToString(kind));
	o.insert(QLatin1String("from"), from);
	if (!to.isEmpty()) o.insert(QLatin1String("to"), to);
	return o;
}

TransactionOp TransactionOp::fromJson(const QJsonObject &o, bool *ok)
{
	if (ok) *ok = false;
	TransactionOp op;

	const QString kindStr = o.value(QLatin1String("kind")).toString();
	Kind k = OpNone;
	for (size_t i = 0; i < KIND_COUNT; i++) {
		if (kindStr == QLatin1String(KIND_NAMES[i].name)) { k = KIND_NAMES[i].kind; break; }
	}
	if (k == OpNone) return op;

	const QString from = o.value(QLatin1String("from")).toString();
	if (from.isEmpty()) return op;

	op.kind = k;
	op.from = from;
	op.to = o.value(QLatin1String("to")).toString();

	/* A move with no destination cannot be undone, so it is not a valid record. */
	if (op.kind == OpMoved && op.to.isEmpty()) return TransactionOp();

	if (ok) *ok = true;
	return op;
}

//----------------------------------------------------------------------

bool TransactionRecord::isValid() const
{
	if (id.isEmpty()) return false;
	if (targetDir.isEmpty()) return false;
	if (packagePath.isEmpty()) return false;
	if (packageSha256.length() != 64) return false;
	if (packageSize <= 0) return false;
	if (state < 0 || state >= TRANSACTION_STATES_COUNT) return false;
	return true;
}

QByteArray TransactionRecord::toJson() const
{
	QJsonObject root;
	/* Versioned so a future format change can be detected rather than
	   misparsed by an older build that finds the file. */
	root.insert(QLatin1String("journalVersion"), 1);
	root.insert(QLatin1String("id"), id);
	root.insert(QLatin1String("state"), transactionStateToString(state));
	root.insert(QLatin1String("targetDir"), targetDir);
	root.insert(QLatin1String("stagingDir"), stagingDir);
	root.insert(QLatin1String("backupDir"), backupDir);
	root.insert(QLatin1String("packagePath"), packagePath);
	root.insert(QLatin1String("packageSize"), double(packageSize));
	root.insert(QLatin1String("packageSha256"), packageSha256);
	root.insert(QLatin1String("fromVersion"), fromVersion);
	root.insert(QLatin1String("toVersion"), toVersion);
	root.insert(QLatin1String("createdAt"), createdAt);
	root.insert(QLatin1String("updatedAt"), updatedAt);
	if (!error.isEmpty()) root.insert(QLatin1String("error"), error);

	QJsonArray opsArray;
	QList<TransactionOp>::const_iterator itr;
	for (itr = ops.begin(); itr != ops.end(); ++itr) {
		opsArray.append(itr->toJson());
	}
	root.insert(QLatin1String("ops"), opsArray);

	/* Indented on purpose: this file gets read by a person after something has
	   gone wrong, which is the only time anyone reads it at all. */
	return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

TransactionRecord TransactionRecord::fromJson(const QByteArray &data, bool *ok)
{
	if (ok) *ok = false;
	TransactionRecord rec;

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		return rec;
	}
	const QJsonObject root = doc.object();

	const int version = root.value(QLatin1String("journalVersion")).toInt(0);
	if (version != 1) {
		/* Written by something else. Refusing is the safe answer: acting on a
		   format we do not understand is worse than reporting it. */
		return rec;
	}

	bool stateOk = false;
	rec.state = transactionStateFromString(root.value(QLatin1String("state")).toString(), &stateOk);

	rec.id            = root.value(QLatin1String("id")).toString();
	rec.targetDir     = root.value(QLatin1String("targetDir")).toString();
	rec.stagingDir    = root.value(QLatin1String("stagingDir")).toString();
	rec.backupDir     = root.value(QLatin1String("backupDir")).toString();
	rec.packagePath   = root.value(QLatin1String("packagePath")).toString();
	rec.packageSize   = qint64(root.value(QLatin1String("packageSize")).toDouble(0));
	rec.packageSha256 = root.value(QLatin1String("packageSha256")).toString();
	rec.fromVersion   = root.value(QLatin1String("fromVersion")).toString();
	rec.toVersion     = root.value(QLatin1String("toVersion")).toString();
	rec.createdAt     = root.value(QLatin1String("createdAt")).toString();
	rec.updatedAt     = root.value(QLatin1String("updatedAt")).toString();
	rec.error         = root.value(QLatin1String("error")).toString();

	const QJsonValue opsValue = root.value(QLatin1String("ops"));
	if (opsValue.isArray()) {
		const QJsonArray opsArray = opsValue.toArray();
		for (int i = 0; i < opsArray.size(); i++) {
			if (!opsArray.at(i).isObject()) return TransactionRecord();
			bool opOk = false;
			const TransactionOp op = TransactionOp::fromJson(opsArray.at(i).toObject(), &opOk);
			/* One unreadable step means the undo sequence is incomplete, and a
			   partial undo is worse than none: reject the whole record. */
			if (!opOk) return TransactionRecord();
			rec.ops.append(op);
		}
	}

	if (!rec.isValid()) return TransactionRecord();
	/* A record that parsed but whose state did not is reported as unreadable,
	   so the caller treats it as needing attention rather than as Prepared. */
	if (ok) *ok = stateOk;
	return rec;
}
