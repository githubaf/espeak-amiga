// AF, GWD, 30.Mar.2017
// do not use gettimeofday() and getpid() (not there in libnix for Amiga !?
// template must not be named template for c++, use Template instead

// taken from https://gitlab.doc.ic.ac.uk/as12312/kstart/blob/956d2bd9e7d28b7332a1e3d7766672357924279f/portable/mkstemp.c

/*
 * Replacement for a missing mkstemp.
 *
 * Provides the same functionality as the library function mkstemp for those
 * systems that don't have it.
 *
 * The canonical version of this file is maintained in the rra-c-util package,
 * which can be found at <http://www.eyrie.org/~eagle/software/rra-c-util/>.
 *
 * Written by Russ Allbery <rra@stanford.edu>
 *
 * The authors hereby relinquish any claim to any copyright that they may have
 * in this work, whether granted under contract or by operation of law or
 * international treaty, and hereby commit to the public, at large, that they
 * shall not, at any time in the future, seek to enforce any copyright in this
 * work against any person or entity, or prevent any person or entity from
 * copying, publishing, distributing or creating derivative works of this
 * work.
 */

//#include <config.h>           // commented AF
//#include <portable/system.h>  // commented AF
#define TMP_MAX 32              // AF The standard defines that the limit is never less than 25. 
#include <stdlib.h>             // AF 
#include <string.h>             // AF
#include <time.h>               // AF

#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

/*
 * If we're running the test suite, rename mkstemp to avoid conflicts with the
 * system version.  #undef it first because some systems may define it to
 * another name.
 */
#if TESTING
# undef mkstemp
# define mkstemp test_mkstemp
int test_mkstemp(char *);
#endif

/* Pick the longest available integer type. */
#if HAVE_LONG_LONG_INT
typedef unsigned long long long_int_type;
#else
typedef unsigned long long_int_type;
#endif

extern "C" int mkstemp(char *Template)     // AF: extern "C"
{
    static const char letters[] =
        //"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";  // AF: lowercase chars only as Amiga-Dos is case insensitive
          "abcdefghijklmnopqrstuvwxyz0123456789";

    size_t length;
    char *XXXXXX;
//    struct timeval tv;   // AF commented because timeofday() not in amiga libnix
    long_int_type randnum, working;
    int i, tries, fd;

    /*
     * Make sure we have a valid template and initialize p to point at the
     * beginning of the template portion of the string.
     */
    length = strlen(Template);
    if (length < 6) {
        errno = EINVAL;
        return -1;
    }
    XXXXXX = Template + length - 6;
    if (strcmp(XXXXXX, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    /* Get some more-or-less random information. */
//    gettimeofday(&tv, NULL);                                             // AF commented, not in Amiga-libnix
//    randnum = ((long_int_type) tv.tv_usec << 16) ^ tv.tv_sec ^ getpid(); // AF commented, not in Amiga libnix

        {                               // AF
		time_t ti;              // AF
		ti=time(NULL);          // AF
        	srand(ti);              // AF seed
        }                               // AF
	randnum=rand();                 // AF 

    /*
     * Now, try to find a working file name.  We try no more than TMP_MAX file
     * names.
     */
    for (tries = 0; tries < TMP_MAX; tries++) {
        for (working = randnum, i = 0; i < 6; i++) {
            XXXXXX[i] = letters[working % 36 /*62*/];   // AF: we dont use uppercase characters, so only 36 entries in letters[]
            working /= 36 /*62*/;                       // AF: we dont use uppercase characters, so only 36 entries in letters[]
        }
        fd = open(Template, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || (errno != EEXIST && errno != EISDIR))
            return fd;

        /*
         * This is a relatively random increment.  Cut off the tail end of
         * tv_usec since it's often predictable.
         */
        // randnum += (tv.tv_usec >> 10) & 0xfff; // commented AF, I did not use timeofday()
	randnum=rand(); // AF
    }
    errno = EEXIST;
    return -1;
}


