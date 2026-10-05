#include "Random.h"

#include <QRandomGenerator>

namespace pe_bear {
namespace updater {

QString randomHex(int characters)
{
	if (characters <= 0) return QString();

	QString hex;
	hex.reserve(characters);
	while (hex.length() < characters) {
		/* Zero-padded to eight characters: without the padding a draw with
		   leading zero bits would contribute fewer than eight, and the result
		   would be shorter than asked for in a way that depends on the value
		   drawn. */
		hex += QString::number(QRandomGenerator::system()->generate(), 16)
			.rightJustified(8, QLatin1Char('0'));
	}
	return hex.left(characters);
}

}; // namespace updater
}; // namespace pe_bear
