/* datasetup.h - where the game data comes from (PORT EXTRA, the original reads it from its CD): finding a copy of the CD files,
 * the first-start copy from the CD and the check against the files of the English 1.00 CD (src/datafiles.h). */
#ifndef DATASETUP_H
#define DATASETUP_H

/* A run without a data dir on the command line. Looks in this order: WOODY_DATA (a Data dir), data\ next to the exe, the CD files
 * straight next to the exe, extract\ in the current directory (a development checkout), %LOCALAPPDATA%\WoodyRE\data. Nothing
 * there: asks for the CD (or a folder with a copy of it) and copies the files into data\ next to the exe, or into
 * %LOCALAPPDATA%\WoodyRE when the exe's folder is read-only. A found or new data\ also makes its folder the current directory,
 * so woodyre.cfg / woodyre.sav live beside it. Returns the Data dir, or NULL when the user gave up (a message was shown). */
const char *data_find(void);

/* --verify: hashes every file of the manifest under <data_dir>\.. and prints the ones that are missing or differ.
 * Returns their number (0 = an exact copy of the English 1.00 CD). */
int data_verify(const char *data_dir);

#endif
