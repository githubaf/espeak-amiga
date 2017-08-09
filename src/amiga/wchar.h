#include <sys/cdefs.h>

#include <stdlib.h>

__BEGIN_DECLS
extern size_t wcslen(const wchar_t *);
extern wchar_t *wcscpy(wchar_t *, const wchar_t *);
extern wchar_t *wcsncpy(wchar_t *, const wchar_t *, int);
extern wchar_t *wcscat(wchar_t *, const wchar_t *);
extern int wcscmp(const wchar_t *, const wchar_t *);
extern int wcsncmp(const wchar_t *, const wchar_t *, int);



extern const wchar_t* wcschr (const wchar_t* ws, wchar_t wc);  //AF
extern double wcstod (const wchar_t* str, wchar_t** endptr);   //AF


__END_DECLS



