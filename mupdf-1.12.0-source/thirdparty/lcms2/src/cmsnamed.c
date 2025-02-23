//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2017 Marti Maria Saguer
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//---------------------------------------------------------------------------------
//

#include "lcms2_internal.h"

// Multilocalized unicode objects. That is an attempt to encapsulate i18n.


// Allocates an empty multi localizad unicode object
cmsMLU* CMSEXPORT cmsMLUalloc(cmsContext ContextID, cmsUInt32Number nItems)
{
    cmsMLU* mlu;

    // nItems should be positive if given
    if (nItems <= 0) nItems = 2;

    // Create the container
    mlu = (cmsMLU*) _cmsMallocZero(ContextID, sizeof(cmsMLU));
    if (mlu == NULL) return NULL;

    // Create entry array
    mlu ->Entries = (_cmsMLUentry*) _cmsCalloc(ContextID, nItems, sizeof(_cmsMLUentry));
    if (mlu ->Entries == NULL) {
        _cmsFree(ContextID, mlu);
        return NULL;
    }

    // Ok, keep indexes up to date
    mlu ->AllocatedEntries    = nItems;
    mlu ->UsedEntries         = 0;

    return mlu;
}


// Grows a mempool table for a MLU. Each time this function is called, mempool size is multiplied times two.
static
cmsBool GrowMLUpool(cmsContext ContextID, cmsMLU* mlu)
{
    cmsUInt32Number size;
    void *NewPtr;

    // Sanity check
    if (mlu == NULL) return FALSE;

    if (mlu ->PoolSize == 0)
        size = 256;
    else
        size = mlu ->PoolSize * 2;

    // Check for overflow
    if (size < mlu ->PoolSize) return FALSE;

    // Reallocate the pool
    NewPtr = _cmsRealloc(ContextID, mlu ->MemPool, size);
    if (NewPtr == NULL) return FALSE;


    mlu ->MemPool  = NewPtr;
    mlu ->PoolSize = size;

    return TRUE;
}


// Grows a entry table for a MLU. Each time this function is called, table size is multiplied times two.
static
cmsBool GrowMLUtable(cmsContext ContextID, cmsMLU* mlu)
{
    cmsUInt32Number AllocatedEntries;
    _cmsMLUentry *NewPtr;

    // Sanity check
    if (mlu == NULL) return FALSE;

    AllocatedEntries = mlu ->AllocatedEntries * 2;

    // Check for overflow
    if (AllocatedEntries / 2 != mlu ->AllocatedEntries) return FALSE;

    // Reallocate the memory
    NewPtr = (_cmsMLUentry*)_cmsRealloc(ContextID, mlu ->Entries, AllocatedEntries*sizeof(_cmsMLUentry));
    if (NewPtr == NULL) return FALSE;

    mlu ->Entries          = NewPtr;
    mlu ->AllocatedEntries = AllocatedEntries;

    return TRUE;
}


// Search for a specific entry in the structure. Language and Country are used.
static
int SearchMLUEntry(cmsMLU* mlu, cmsUInt16Number LanguageCode, cmsUInt16Number CountryCode)
{
    cmsUInt32Number i;

    // Sanity check
    if (mlu == NULL) return -1;

    // Iterate whole table
    for (i=0; i < mlu ->UsedEntries; i++) {

        if (mlu ->Entries[i].Country  == CountryCode &&
            mlu ->Entries[i].Language == LanguageCode) return (int) i;
    }

    // Not found
    return -1;
}

// Add a block of characters to the intended MLU. Language and country are specified.
// Only one entry for Language/country pair is allowed.
static
cmsBool AddMLUBlock(cmsContext ContextID, cmsMLU* mlu, cmsUInt32Number size, const wchar_t *Block,
                     cmsUInt16Number LanguageCode, cmsUInt16Number CountryCode)
{
    cmsUInt32Number Offset;
    cmsUInt8Number* Ptr;

    // Sanity check
    if (mlu == NULL) return FALSE;

    // Is there any room available?
    if (mlu ->UsedEntries >= mlu ->AllocatedEntries) {
        if (!GrowMLUtable(ContextID, mlu)) return FALSE;
    }

    // Only one ASCII string
    if (SearchMLUEntry(mlu, LanguageCode, CountryCode) >= 0) return FALSE;  // Only one  is allowed!

    // Check for size
    while ((mlu ->PoolSize - mlu ->PoolUsed) < size) {

            if (!GrowMLUpool(ContextID, mlu)) return FALSE;
    }

    Offset = mlu ->PoolUsed;

    Ptr = (cmsUInt8Number*) mlu ->MemPool;
    if (Ptr == NULL) return FALSE;

    // Set the entry
    memmove(Ptr + Offset, Block, size);
    mlu ->PoolUsed += size;

    mlu ->Entries[mlu ->UsedEntries].StrW     = Offset;
    mlu ->Entries[mlu ->UsedEntries].Len      = size;
    mlu ->Entries[mlu ->UsedEntries].Country  = CountryCode;
    mlu ->Entries[mlu ->UsedEntries].Language = LanguageCode;
    mlu ->UsedEntries++;

    return TRUE;
}

// Convert from a 3-char code to a cmsUInt16Number. It is done in this way because some
// compilers don't properly align beginning of strings

static
cmsUInt16Number strTo16(cmsContext ContextID, const char str[3])
{
    const cmsUInt8Number* ptr8 = (const cmsUInt8Number*)str;
    cmsUInt16Number n = (cmsUInt16Number) (((cmsUInt16Number) ptr8[1] << 8) | ptr8[0]);

    return _cmsAdjustEndianess16(ContextID, n);
}

static
void strFrom16(cmsContext ContextID, char str[3], cmsUInt16Number n)
{
    // Assuming this would be aligned
    union {

       cmsUInt16Number n;
       cmsUInt8Number str[2];

    } c;

    c.n = _cmsAdjustEndianess16(ContextID, n);

    str[0] = (char) c.str[0]; str[1] = (char) c.str[1]; str[2] = (char) 0;

}

// Add an ASCII entry. Do not add any \0 termination (ICC1v43_2010-12.pdf page 61)
cmsBool CMSEXPORT cmsMLUsetASCII(cmsContext ContextID, cmsMLU* mlu, const char LanguageCode[3], const char CountryCode[3], const char* ASCIIString)
{
    cmsUInt32Number i, len = (cmsUInt32Number) strlen(ASCIIString);
    wchar_t* WStr;
    cmsBool  rc;
    cmsUInt16Number Lang  = strTo16(ContextID, LanguageCode);
    cmsUInt16Number Cntry = strTo16(ContextID, CountryCode);

    if (mlu == NULL) return FALSE;

    WStr = (wchar_t*) _cmsCalloc(ContextID, len,  sizeof(wchar_t));
    if (WStr == NULL) return FALSE;

    for (i=0; i < len; i++)
        WStr[i] = (wchar_t) ASCIIString[i];

    rc = AddMLUBlock(ContextID, mlu, len  * sizeof(wchar_t), WStr, Lang, Cntry);

    _cmsFree(ContextID, WStr);
    return rc;

}

// We don't need any wcs support library
static
cmsUInt32Number mywcslen(const wchar_t *s)
{
    const wchar_t *p;

    p = s;
    while (*p)
        p++;

    return (cmsUInt32Number)(p - s);
}

// Add a wide entry. Do not add any \0 terminator (ICC1v43_2010-12.pdf page 61)
cmsBool  CMSEXPORT cmsMLUsetWide(cmsContext ContextID, cmsMLU* mlu, const char Language[3], const char Country[3], const wchar_t* WideString)
{
    cmsUInt16Number Lang  = strTo16(ContextID, Language);
    cmsUInt16Number Cntry = strTo16(ContextID, Country);
    cmsUInt32Number len;

    if (mlu == NULL) return FALSE;
    if (WideString == NULL) return FALSE;

    len = (cmsUInt32Number) (mywcslen(WideString)) * sizeof(wchar_t);
    return AddMLUBlock(ContextID, mlu, len, WideString, Lang, Cntry);
}

// Duplicating a MLU is as easy as copying all members
cmsMLU* CMSEXPORT cmsMLUdup(cmsContext ContextID, const cmsMLU* mlu)
{
    cmsMLU* NewMlu = NULL;

    // Duplicating a NULL obtains a NULL
    if (mlu == NULL) return NULL;

    NewMlu = cmsMLUalloc(ContextID, mlu ->UsedEntries);
    if (NewMlu == NULL) return NULL;

    // Should never happen
    if (NewMlu ->AllocatedEntries < mlu ->UsedEntries)
        goto Error;

    // Sanitize...
    if (NewMlu ->Entries == NULL || mlu ->Entries == NULL)  goto Error;

    memmove(NewMlu ->Entries, mlu ->Entries, mlu ->UsedEntries * sizeof(_cmsMLUentry));
    NewMlu ->UsedEntries = mlu ->UsedEntries;

    // The MLU may be empty
    if (mlu ->PoolUsed == 0) {
        NewMlu ->MemPool = NULL;
    }
    else {
        // It is not empty
        NewMlu ->MemPool = _cmsMalloc(ContextID, mlu ->PoolUsed);
        if (NewMlu ->MemPool == NULL) goto Error;
    }

    NewMlu ->PoolSize = mlu ->PoolUsed;

    if (NewMlu ->MemPool == NULL || mlu ->MemPool == NULL) goto Error;

    memmove(NewMlu ->MemPool, mlu->MemPool, mlu ->PoolUsed);
    NewMlu ->PoolUsed = mlu ->PoolUsed;

    return NewMlu;

Error:

    if (NewMlu != NULL) cmsMLUfree(ContextID, NewMlu);
    return NULL;
}

// Free any used memory
void CMSEXPORT cmsMLUfree(cmsContext ContextID, cmsMLU* mlu)
{
    if (mlu) {

        if (mlu -> Entries) _cmsFree(ContextID, mlu->Entries);
        if (mlu -> MemPool) _cmsFree(ContextID, mlu->MemPool);

        _cmsFree(ContextID, mlu);
    }
}


// The algorithm first searches for an exact match of country and language, if not found it uses
// the Language. If none is found, first entry is used instead.
static
const wchar_t* _cmsMLUgetWide(const cmsMLU* mlu,
                              cmsUInt32Number *len,
                              cmsUInt16Number LanguageCode, cmsUInt16Number CountryCode,
                              cmsUInt16Number* UsedLanguageCode, cmsUInt16Number* UsedCountryCode)
{
    cmsUInt32Number i;
    int Best = -1;
    _cmsMLUentry* v;

    if (mlu == NULL) return NULL;

    if (mlu -> AllocatedEntries <= 0) return NULL;

    for (i=0; i < mlu ->UsedEntries; i++) {

        v = mlu ->Entries + i;

        if (v -> Language == LanguageCode) {

            if (Best == -1) Best = (int) i;

            if (v -> Country == CountryCode) {

                if (UsedLanguageCode != NULL) *UsedLanguageCode = v ->Language;
                if (UsedCountryCode  != NULL) *UsedCountryCode = v ->Country;

                if (len != NULL) *len = v ->Len;

                return (wchar_t*) ((cmsUInt8Number*) mlu ->MemPool + v -> StrW);        // Found exact match
            }
        }
    }

    // No string found. Return First one
    if (Best == -1)
        Best = 0;

    v = mlu ->Entries + Best;

    if (UsedLanguageCode != NULL) *UsedLanguageCode = v ->Language;
    if (UsedCountryCode  != NULL) *UsedCountryCode = v ->Country;

    if (len != NULL) *len   = v ->Len;

    return(wchar_t*) ((cmsUInt8Number*) mlu ->MemPool + v ->StrW);
}


// Obtain an ASCII representation of the wide string. Setting buffer to NULL returns the len
cmsUInt32Number CMSEXPORT cmsMLUgetASCII(cmsContext ContextID, const cmsMLU* mlu,
                                       const char LanguageCode[3], const char CountryCode[3],
                                       char* Buffer, cmsUInt32Number BufferSize)
{
    const wchar_t *Wide;
    cmsUInt32Number  StrLen = 0;
    cmsUInt32Number ASCIIlen, i;

    cmsUInt16Number Lang  = strTo16(ContextID, LanguageCode);
    cmsUInt16Number Cntry = strTo16(ContextID, CountryCode);

    // Sanitize
    if (mlu == NULL) return 0;

    // Get WideChar
    Wide = _cmsMLUgetWide(mlu, &StrLen, Lang, Cntry, NULL, NULL);
    if (Wide == NULL) return 0;

    ASCIIlen = StrLen / sizeof(wchar_t);

    // Maybe we want only to know the len?
    if (Buffer == NULL) return ASCIIlen + 1; // Note the zero at the end

    // No buffer size means no data
    if (BufferSize <= 0) return 0;

    // Some clipping may be required
    if (BufferSize < ASCIIlen + 1)
        ASCIIlen = BufferSize - 1;

    // Precess each character
    for (i=0; i < ASCIIlen; i++) {

        if (Wide[i] == 0)
            Buffer[i] = 0;
        else
            Buffer[i] = (char) Wide[i];
    }

    // We put a termination "\0"
    Buffer[ASCIIlen] = 0;
    return ASCIIlen + 1;
}

// Obtain a wide representation of the MLU, on depending on current locale settings
cmsUInt32Number CMSEXPORT cmsMLUgetWide(cmsContext ContextID, const cmsMLU* mlu,
                                      const char LanguageCode[3], const char CountryCode[3],
                                      wchar_t* Buffer, cmsUInt32Number BufferSize)
{
    const wchar_t *Wide;
    cmsUInt32Number  StrLen = 0;

    cmsUInt16Number Lang  = strTo16(ContextID, LanguageCode);
    cmsUInt16Number Cntry = strTo16(ContextID, CountryCode);

    // Sanitize
    if (mlu == NULL) return 0;

    Wide = _cmsMLUgetWide(mlu, &StrLen, Lang, Cntry, NULL, NULL);
    if (Wide == NULL) return 0;

    // Maybe we want only to know the len?
    if (Buffer == NULL) return StrLen + sizeof(wchar_t);

  // No buffer size means no data
    if (BufferSize <= 0) return 0;

    // Some clipping may be required
    if (BufferSize < StrLen + sizeof(wchar_t))
        StrLen = BufferSize - + sizeof(wchar_t);

    memmove(Buffer, Wide, StrLen);
    Buffer[StrLen / sizeof(wchar_t)] = 0;

    return StrLen + sizeof(wchar_t);
}


// Get also the language and country
CMSAPI cmsBool CMSEXPORT cmsMLUgetTranslation(cmsContext ContextID, const cmsMLU* mlu,
                                              const char LanguageCode[3], const char CountryCode[3],
                                              char ObtainedLanguage[3], char ObtainedCountry[3])
{
    const wchar_t *Wide;

    cmsUInt16Number Lang  = strTo16(ContextID, LanguageCode);
    cmsUInt16Number Cntry = strTo16(ContextID, CountryCode);
    cmsUInt16Number ObtLang, ObtCode;

    // Sanitize
    if (mlu == NULL) return FALSE;

    Wide = _cmsMLUgetWide(mlu, NULL, Lang, Cntry, &ObtLang, &ObtCode);
    if (Wide == NULL) return FALSE;

    // Get used language and code
    strFrom16(ContextID, ObtainedLanguage, ObtLang);
    strFrom16(ContextID, ObtainedCountry, ObtCode);

    return TRUE;
}



// Get the number of translations in the MLU object
cmsUInt32Number CMSEXPORT cmsMLUtranslationsCount(cmsContext ContextID, const cmsMLU* mlu)
{
    cmsUNUSED_PARAMETER(ContextID);
    if (mlu == NULL) return 0;
    return mlu->UsedEntries;
}

// Get the language and country codes for a specific MLU index
cmsBool CMSEXPORT cmsMLUtranslationsCodes(cmsContext ContextID,
                                          const cmsMLU* mlu,
                                          cmsUInt32Number idx,
                                          char LanguageCode[3],
                                          char CountryCode[3])
{
    _cmsMLUentry *entry;

    if (mlu == NULL) return FALSE;

    if (idx >= mlu->UsedEntries) return FALSE;

    entry = &mlu->Entries[idx];

    strFrom16(ContextID, LanguageCode, entry->Language);
    strFrom16(ContextID, CountryCode, entry->Country);

    return TRUE;
}


// Named color lists --------------------------------------------------------------------------------------------

// Grow the list to keep at least NumElements
static
cmsBool  GrowNamedColorList(cmsContext ContextID, cmsNAMEDCOLORLIST* v)
{
    cmsUInt32Number size;
    _cmsNAMEDCOLOR * NewPtr;

    if (v == NULL) return FALSE;

    if (v ->Allocated == 0)
        size = 64;   // Initial guess
    else
        size = v ->Allocated * 2;

    // Keep a maximum color lists can grow, 100K entries seems reasonable
    if (size > 1024 * 100) {
        _cmsFree(ContextID, (void*) v->List);
        v->List = NULL;
        return FALSE;
    }

    NewPtr = (_cmsNAMEDCOLOR*) _cmsRealloc(ContextID, v ->List, size * sizeof(_cmsNAMEDCOLOR));
    if (NewPtr == NULL)
        return FALSE;

    v ->List      = NewPtr;
    v ->Allocated = size;
    return TRUE;
}

// Allocate a list for n elements
cmsNAMEDCOLORLIST* CMSEXPORT cmsAllocNamedColorList(cmsContext ContextID, cmsUInt32Number n, cmsUInt32Number ColorantCount, const char* Prefix, const char* Suffix)
{
    cmsNAMEDCOLORLIST* v = (cmsNAMEDCOLORLIST*) _cmsMallocZero(ContextID, sizeof(cmsNAMEDCOLORLIST));

    if (v == NULL) return NULL;

    v ->List      = NULL;
    v ->nColors   = 0;

    while (v -> Allocated < n) {
        if (!GrowNamedColorList(ContextID, v)) {
            _cmsFree(ContextID, (void*) v);
            return NULL;
        }
    }

    strncpy(v ->Prefix, Prefix, sizeof(v ->Prefix)-1);
    strncpy(v ->Suffix, Suffix, sizeof(v ->Suffix)-1);
    v->Prefix[32] = v->Suffix[32] = 0;

    v -> ColorantCount = ColorantCount;

    return v;
}

// Free a list
void CMSEXPORT cmsFreeNamedColorList(cmsContext ContextID, cmsNAMEDCOLORLIST* v)
{
    if (v == NULL) return;
    if (v ->List) _cmsFree(ContextID, v ->List);
    _cmsFree(ContextID, v);
}

cmsNAMEDCOLORLIST* CMSEXPORT cmsDupNamedColorList(cmsContext ContextID, const cmsNAMEDCOLORLIST* v)
{
    cmsNAMEDCOLORLIST* NewNC;

    if (v == NULL) return NULL;

    NewNC= cmsAllocNamedColorList(ContextID, v -> nColors, v ->ColorantCount, v ->Prefix, v ->Suffix);
    if (NewNC == NULL) return NULL;

    // For really large tables we need this
    while (NewNC ->Allocated < v ->Allocated){
        if (!GrowNamedColorList(ContextID, NewNC)) return NULL;
    }

    memmove(NewNC ->Prefix, v ->Prefix, sizeof(v ->Prefix));
    memmove(NewNC ->Suffix, v ->Suffix, sizeof(v ->Suffix));
    NewNC ->ColorantCount = v ->ColorantCount;
    memmove(NewNC->List, v ->List, v->nColors * sizeof(_cmsNAMEDCOLOR));
    NewNC ->nColors = v ->nColors;
    return NewNC;
}


// Append a color to a list. List pointer may change if reallocated
cmsBool  CMSEXPORT cmsAppendNamedColor(cmsContext ContextID, cmsNAMEDCOLORLIST* NamedColorList,
                                       const char* Name,
                                       cmsUInt16Number PCS[3], cmsUInt16Number Colorant[cmsMAXCHANNELS])
{
    cmsUInt32Number i;

    if (NamedColorList == NULL) return FALSE;

    if (NamedColorList ->nColors + 1 > NamedColorList ->Allocated) {
        if (!GrowNamedColorList(ContextID, NamedColorList)) return FALSE;
    }

    for (i=0; i < NamedColorList ->ColorantCount; i++)
        NamedColorList ->List[NamedColorList ->nColors].DeviceColorant[i] = Colorant == NULL ? (cmsUInt16Number)0 : Colorant[i];

    for (i=0; i < 3; i++)
        NamedColorList ->List[NamedColorList ->nColors].PCS[i] = PCS == NULL ? (cmsUInt16Number) 0 : PCS[i];

    if (Name != NULL) {

        strncpy(NamedColorList ->List[NamedColorList ->nColors].Name, Name, cmsMAX_PATH-1);
        NamedColorList ->List[NamedColorList ->nColors].Name[cmsMAX_PATH-1] = 0;

    }
    else
        NamedColorList ->List[NamedColorList ->nColors].Name[0] = 0;


    NamedColorList ->nColors++;
    return TRUE;
}

// Returns number of elements
cmsUInt32Number CMSEXPORT cmsNamedColorCount(cmsContext ContextID, const cmsNAMEDCOLORLIST* NamedColorList)
{
     cmsUNUSED_PARAMETER(ContextID);
     if (NamedColorList == NULL) return 0;
     return NamedColorList ->nColors;
}

// Info aboout a given color
cmsBool  CMSEXPORT cmsNamedColorInfo(cmsContext ContextID, const cmsNAMEDCOLORLIST* NamedColorList, cmsUInt32Number nColor,
                                     char* Name,
                                     char* Prefix,
                                     char* Suffix,
                                     cmsUInt16Number* PCS,
                                     cmsUInt16Number* Colorant)
{
    if (NamedColorList == NULL) return FALSE;

    if (nColor >= cmsNamedColorCount(ContextID, NamedColorList)) return FALSE;

    // strcpy instead of strncpy because many apps are using small buffers
    if (Name) strcpy(Name, NamedColorList->List[nColor].Name);
    if (Prefix) strcpy(Prefix, NamedColorList->Prefix);
    if (Suffix) strcpy(Suffix, NamedColorList->Suffix);
    if (PCS)
        memmove(PCS, NamedColorList ->List[nColor].PCS, 3*sizeof(cmsUInt16Number));

    if (Colorant)
        memmove(Colorant, NamedColorList ->List[nColor].DeviceColorant,
                                sizeof(cmsUInt16Number) * NamedColorList ->ColorantCount);


    return TRUE;
}

// Search for a given color name (no prefix or suffix)
cmsInt32Number CMSEXPORT cmsNamedColorIndex(cmsContext ContextID, const cmsNAMEDCOLORLIST* NamedColorList, const char* Name)
{
    cmsUInt32Number i;
    cmsUInt32Number n;

    if (NamedColorList == NULL) return -1;
    n = cmsNamedColorCount(ContextID, NamedColorList);
    for (i=0; i < n; i++) {
        if (cmsstrcasecmp(Name,  NamedColorList->List[i].Name) == 0)
            return (cmsInt32Number) i;
    }

    return -1;
}

// MPE support -----------------------------------------------------------------------------------------------------------------

static
void FreeNamedColorList(cmsContext ContextID, cmsStage* mpe)
{
    cmsNAMEDCOLORLIST* List = (cmsNAMEDCOLORLIST*) mpe ->Data;
    cmsFreeNamedColorList(ContextID, List);
}

static
void* DupNamedColorList(cmsContext ContextID, cmsStage* mpe)
{
    cmsNAMEDCOLORLIST* List = (cmsNAMEDCOLORLIST*) mpe ->Data;
    return cmsDupNamedColorList(ContextID, List);
}

static
void EvalNamedColorPCS(cmsContext ContextID, const cmsFloat32Number In[], cmsFloat32Number Out[], const cmsStage *mpe)
{
    cmsNAMEDCOLORLIST* NamedColorList = (cmsNAMEDCOLORLIST*) mpe ->Data;
    cmsUInt16Number index = (cmsUInt16Number) _cmsQuickSaturateWord(In[0] * 65535.0);

    if (index >= NamedColorList-> nColors) {
        cmsSignalError(ContextID, cmsERROR_RANGE, "Color %d out of range; ignored", index);
        Out[0] = Out[1] = Out[2] = 0.0f;
    }
    else {

            // Named color always uses Lab
            Out[0] = (cmsFloat32Number) (NamedColorList->List[index].PCS[0] / 65535.0);
            Out[1] = (cmsFloat32Number) (NamedColorList->List[index].PCS[1] / 65535.0);
            Out[2] = (cmsFloat32Number) (NamedColorList->List[index].PCS[2] / 65535.0);
    }
}

static
void EvalNamedColor(cmsContext ContextID, const cmsFloat32Number In[], cmsFloat32Number Out[], const cmsStage *mpe)
{
    cmsNAMEDCOLORLIST* NamedColorList = (cmsNAMEDCOLORLIST*) mpe ->Data;
    cmsUInt16Number index = (cmsUInt16Number) _cmsQuickSaturateWord(In[0] * 65535.0);
    cmsUInt32Number j;

    if (index >= NamedColorList-> nColors) {
        cmsSignalError(ContextID, cmsERROR_RANGE, "Color %d out of range; ignored", index);
        for (j = 0; j < NamedColorList->ColorantCount; j++)
            Out[j] = 0.0f;
    }
    else {
        for (j=0; j < NamedColorList ->ColorantCount; j++)
            Out[j] = (cmsFloat32Number) (NamedColorList->List[index].DeviceColorant[j] / 65535.0);
    }
}


// Named color lookup element
cmsStage* _cmsStageAllocNamedColor(cmsContext ContextID, cmsNAMEDCOLORLIST* NamedColorList, cmsBool UsePCS)
{
    return _cmsStageAllocPlaceholder(ContextID,
                                   cmsSigNamedColorElemType,
                                   1, UsePCS ? 3 : NamedColorList ->ColorantCount,
                                   UsePCS ? EvalNamedColorPCS : EvalNamedColor,
                                   DupNamedColorList,
                                   FreeNamedColorList,
                                   cmsDupNamedColorList(ContextID, NamedColorList));

}


// Retrieve the named color list from a transform. Should be first element in the LUT
cmsNAMEDCOLORLIST* CMSEXPORT cmsGetNamedColorList(cmsHTRANSFORM xform)
{
    _cmsTRANSFORM* v = (_cmsTRANSFORM*) xform;
    cmsStage* mpe  = v ->Lut->Elements;

    if (mpe ->Type != cmsSigNamedColorElemType) return NULL;
    return (cmsNAMEDCOLORLIST*) mpe ->Data;
}


// Profile sequence description routines -------------------------------------------------------------------------------------

cmsSEQ* CMSEXPORT cmsAllocProfileSequenceDescription(cmsContext ContextID, cmsUInt32Number n)
{
    cmsSEQ* Seq;
    cmsUInt32Number i;

    if (n == 0) return NULL;

    // In a absolutely arbitrary way, I hereby decide to allow a maxim of 255 profiles linked
    // in a devicelink. It makes not sense anyway and may be used for exploits, so let's close the door!
    if (n > 255) return NULL;

    Seq = (cmsSEQ*) _cmsMallocZero(ContextID, sizeof(cmsSEQ));
    if (Seq == NULL) return NULL;

    Seq -> seq      = (cmsPSEQDESC*) _cmsCalloc(ContextID, n, sizeof(cmsPSEQDESC));
    Seq -> n        = n;

    if (Seq -> seq == NULL) {
        _cmsFree(ContextID, Seq);
        return NULL;
    }

    for (i=0; i < n; i++) {
        Seq -> seq[i].Manufacturer = NULL;
        Seq -> seq[i].Model        = NULL;
        Seq -> seq[i].Description  = NULL;
    }

    return Seq;
}

void CMSEXPORT cmsFreeProfileSequenceDescription(cmsContext ContextID, cmsSEQ* pseq)
{
    cmsUInt32Number i;

    for (i=0; i < pseq ->n; i++) {
        if (pseq ->seq[i].Manufacturer != NULL) cmsMLUfree(ContextID, pseq ->seq[i].Manufacturer);
        if (pseq ->seq[i].Model != NULL) cmsMLUfree(ContextID, pseq ->seq[i].Model);
        if (pseq ->seq[i].Description != NULL) cmsMLUfree(ContextID, pseq ->seq[i].Description);
    }

    if (pseq ->seq != NULL) _cmsFree(ContextID, pseq ->seq);
    _cmsFree(ContextID, pseq);
}

cmsSEQ* CMSEXPORT cmsDupProfileSequenceDescription(cmsContext ContextID, const cmsSEQ* pseq)
{
    cmsSEQ *NewSeq;
    cmsUInt32Number i;

    if (pseq == NULL)
        return NULL;

    NewSeq = (cmsSEQ*) _cmsMalloc(ContextID, sizeof(cmsSEQ));
    if (NewSeq == NULL) return NULL;


    NewSeq -> seq      = (cmsPSEQDESC*) _cmsCalloc(ContextID, pseq ->n, sizeof(cmsPSEQDESC));
    if (NewSeq ->seq == NULL) goto Error;

    NewSeq -> n        = pseq ->n;

    for (i=0; i < pseq->n; i++) {

        memmove(&NewSeq ->seq[i].attributes, &pseq ->seq[i].attributes, sizeof(cmsUInt64Number));

        NewSeq ->seq[i].deviceMfg   = pseq ->seq[i].deviceMfg;
        NewSeq ->seq[i].deviceModel = pseq ->seq[i].deviceModel;
        memmove(&NewSeq ->seq[i].ProfileID, &pseq ->seq[i].ProfileID, sizeof(cmsProfileID));
        NewSeq ->seq[i].technology  = pseq ->seq[i].technology;

        NewSeq ->seq[i].Manufacturer = cmsMLUdup(ContextID, pseq ->seq[i].Manufacturer);
        NewSeq ->seq[i].Model        = cmsMLUdup(ContextID, pseq ->seq[i].Model);
        NewSeq ->seq[i].Description  = cmsMLUdup(ContextID, pseq ->seq[i].Description);

    }

    return NewSeq;

Error:

    cmsFreeProfileSequenceDescription(ContextID, NewSeq);
    return NULL;
}

// Dictionaries --------------------------------------------------------------------------------------------------------

// Dictionaries are just very simple linked lists


typedef struct _cmsDICT_struct {
    cmsDICTentry* head;
} _cmsDICT;


// Allocate an empty dictionary
cmsHANDLE CMSEXPORT cmsDictAlloc(cmsContext ContextID)
{
    _cmsDICT* dict = (_cmsDICT*) _cmsMallocZero(ContextID, sizeof(_cmsDICT));
    if (dict == NULL) return NULL;

    return (cmsHANDLE) dict;

}

// Dispose resources
void CMSEXPORT cmsDictFree(cmsContext ContextID, cmsHANDLE hDict)
{
    _cmsDICT* dict = (_cmsDICT*) hDict;
    cmsDICTentry *entry, *next;

    _cmsAssert(dict != NULL);

    // Walk the list freeing all nodes
    entry = dict ->head;
    while (entry != NULL) {

            if (entry ->DisplayName  != NULL) cmsMLUfree(ContextID, entry ->DisplayName);
            if (entry ->DisplayValue != NULL) cmsMLUfree(ContextID, entry ->DisplayValue);
            if (entry ->Name != NULL) _cmsFree(ContextID, entry -> Name);
            if (entry ->Value != NULL) _cmsFree(ContextID, entry -> Value);

            // Don't fall in the habitual trap...
            next = entry ->Next;
            _cmsFree(ContextID, entry);

            entry = next;
    }

    _cmsFree(ContextID, dict);
}


// Duplicate a wide char string
static
wchar_t* DupWcs(cmsContext ContextID, const wchar_t* ptr)
{
    if (ptr == NULL) return NULL;
    return (wchar_t*) _cmsDupMem(ContextID, ptr, (mywcslen(ptr) + 1) * sizeof(wchar_t));
}

// Add a new entry to the linked list
cmsBool CMSEXPORT cmsDictAddEntry(cmsContext ContextID, cmsHANDLE hDict, const wchar_t* Name, const wchar_t* Value, const cmsMLU *DisplayName, const cmsMLU *DisplayValue)
{
    _cmsDICT* dict = (_cmsDICT*) hDict;
    cmsDICTentry *entry;

    _cmsAssert(dict != NULL);
    _cmsAssert(Name != NULL);

    entry = (cmsDICTentry*) _cmsMallocZero(ContextID, sizeof(cmsDICTentry));
    if (entry == NULL) return FALSE;

    entry ->DisplayName  = cmsMLUdup(ContextID, DisplayName);
    entry ->DisplayValue = cmsMLUdup(ContextID, DisplayValue);
    entry ->Name         = DupWcs(ContextID, Name);
    entry ->Value        = DupWcs(ContextID, Value);

    entry ->Next = dict ->head;
    dict ->head = entry;

    return TRUE;
}


// Duplicates an existing dictionary
cmsHANDLE CMSEXPORT cmsDictDup(cmsContext ContextID, cmsHANDLE hDict)
{
    _cmsDICT* old_dict = (_cmsDICT*) hDict;
    cmsHANDLE hNew;
    cmsDICTentry *entry;

    _cmsAssert(old_dict != NULL);

    hNew  = cmsDictAlloc(ContextID);
    if (hNew == NULL) return NULL;

    // Walk the list freeing all nodes
    entry = old_dict ->head;
    while (entry != NULL) {

        if (!cmsDictAddEntry(ContextID, hNew, entry ->Name, entry ->Value, entry ->DisplayName, entry ->DisplayValue)) {

            cmsDictFree(ContextID, hNew);
            return NULL;
        }

        entry = entry -> Next;
    }

    return hNew;
}

// Get a pointer to the linked list
const cmsDICTentry* CMSEXPORT cmsDictGetEntryList(cmsContext ContextID, cmsHANDLE hDict)
{
    _cmsDICT* dict = (_cmsDICT*) hDict;
    cmsUNUSED_PARAMETER(ContextID);

    if (dict == NULL) return NULL;
    return dict ->head;
}

// Helper For external languages
const cmsDICTentry* CMSEXPORT cmsDictNextEntry(cmsContext ContextID, const cmsDICTentry* e)
{
    cmsUNUSED_PARAMETER(ContextID);
    if (e == NULL) return NULL;
     return e ->Next;
}
