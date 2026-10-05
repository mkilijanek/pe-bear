#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * Unpredictable lowercase hex, @p characters long.
 *
 * One implementation for every nonce and run id in the updater. There were
 * two, written the same way minutes apart, and that is the problem rather
 * than the duplication: the hardening decision below has to hold for all of
 * them, and a second copy is where it silently stops holding.
 *
 * It draws from QRandomGenerator::system(), not global(). The difference
 * matters for the startup handshake: global() is seeded from the system
 * entropy source but is then an ordinary deterministic generator, so anyone
 * who learns its state can predict its future output. system() goes to the
 * operating system's cryptographic generator every time. The handshake nonce
 * is what makes a forged "I started fine" response useless, so predictable is
 * not good enough -- and nothing here is hot enough for the cost to matter.
 *
 * Returns an empty string for a non-positive length.
 */
QString randomHex(int characters);

}; // namespace updater
}; // namespace pe_bear
