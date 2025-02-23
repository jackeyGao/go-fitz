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

// Transformations stuff
// -----------------------------------------------------------------------

#define DEFAULT_OBSERVER_ADAPTATION_STATE 1.0

// The Context0 observer adaptation state.
_cmsAdaptationStateChunkType _cmsAdaptationStateChunk = { DEFAULT_OBSERVER_ADAPTATION_STATE };

// Init and duplicate observer adaptation state
void _cmsAllocAdaptationStateChunk(struct _cmsContext_struct* ctx,
                                   const struct _cmsContext_struct* src)
{
    static _cmsAdaptationStateChunkType AdaptationStateChunk = { DEFAULT_OBSERVER_ADAPTATION_STATE };
    void* from;

    if (src != NULL) {
        from = src ->chunks[AdaptationStateContext];
    }
    else {
       from = &AdaptationStateChunk;
    }

    ctx ->chunks[AdaptationStateContext] = _cmsSubAllocDup(ctx ->MemPool, from, sizeof(_cmsAdaptationStateChunkType));
}


// Sets adaptation state for absolute colorimetric intent in the given context.  Adaptation state applies on all
// but cmsCreateExtendedTransformTHR().  Little CMS can handle incomplete adaptation states.
cmsFloat64Number CMSEXPORT cmsSetAdaptationStateTHR(cmsContext ContextID, cmsFloat64Number d)
{
    cmsFloat64Number prev;
    _cmsAdaptationStateChunkType* ptr = (_cmsAdaptationStateChunkType*) _cmsContextGetClientChunk(ContextID, AdaptationStateContext);

    // Get previous value for return
    prev = ptr ->AdaptationState;

    // Set the value if d is positive or zero
    if (d >= 0.0) {

        ptr ->AdaptationState = d;
    }

    // Always return previous value
    return prev;
}


// The adaptation state may be defaulted by this function. If you don't like it, use the extended transform routine
cmsFloat64Number CMSEXPORT cmsSetAdaptationState(cmsFloat64Number d)
{
    return cmsSetAdaptationStateTHR(NULL, d);
}

// -----------------------------------------------------------------------

// Alarm codes for 16-bit transformations, because the fixed range of containers there are
// no values left to mark out of gamut.

#define DEFAULT_ALARM_CODES_VALUE {0x7F00, 0x7F00, 0x7F00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

_cmsAlarmCodesChunkType _cmsAlarmCodesChunk = { DEFAULT_ALARM_CODES_VALUE };

// Sets the codes used to mark out-out-gamut on Proofing transforms for a given context. Values are meant to be
// encoded in 16 bits.
void CMSEXPORT cmsSetAlarmCodesTHR(cmsContext ContextID, const cmsUInt16Number AlarmCodesP[cmsMAXCHANNELS])
{
    _cmsAlarmCodesChunkType* ContextAlarmCodes = (_cmsAlarmCodesChunkType*) _cmsContextGetClientChunk(ContextID, AlarmCodesContext);

    _cmsAssert(ContextAlarmCodes != NULL); // Can't happen

    memcpy(ContextAlarmCodes->AlarmCodes, AlarmCodesP, sizeof(ContextAlarmCodes->AlarmCodes));
}

// Gets the current codes used to mark out-out-gamut on Proofing transforms for the given context.
// Values are meant to be encoded in 16 bits.
void CMSEXPORT cmsGetAlarmCodesTHR(cmsContext ContextID, cmsUInt16Number AlarmCodesP[cmsMAXCHANNELS])
{
    _cmsAlarmCodesChunkType* ContextAlarmCodes = (_cmsAlarmCodesChunkType*) _cmsContextGetClientChunk(ContextID, AlarmCodesContext);

    _cmsAssert(ContextAlarmCodes != NULL); // Can't happen

    memcpy(AlarmCodesP, ContextAlarmCodes->AlarmCodes, sizeof(ContextAlarmCodes->AlarmCodes));
}

void CMSEXPORT cmsSetAlarmCodes(const cmsUInt16Number NewAlarm[cmsMAXCHANNELS])
{
    _cmsAssert(NewAlarm != NULL);

    cmsSetAlarmCodesTHR(NULL, NewAlarm);
}

void CMSEXPORT cmsGetAlarmCodes(cmsUInt16Number OldAlarm[cmsMAXCHANNELS])
{
    _cmsAssert(OldAlarm != NULL);
    cmsGetAlarmCodesTHR(NULL, OldAlarm);
}


// Init and duplicate alarm codes
void _cmsAllocAlarmCodesChunk(struct _cmsContext_struct* ctx,
                              const struct _cmsContext_struct* src)
{
    static _cmsAlarmCodesChunkType AlarmCodesChunk = { DEFAULT_ALARM_CODES_VALUE };
    void* from;

    if (src != NULL) {
        from = src ->chunks[AlarmCodesContext];
    }
    else {
       from = &AlarmCodesChunk;
    }

    ctx ->chunks[AlarmCodesContext] = _cmsSubAllocDup(ctx ->MemPool, from, sizeof(_cmsAlarmCodesChunkType));
}

// -----------------------------------------------------------------------

// Get rid of transform resources
void CMSEXPORT cmsDeleteTransform(cmsContext ContextID, cmsHTRANSFORM hTransform)
{
    _cmsTRANSFORM* p = (_cmsTRANSFORM*) hTransform;

    _cmsAssert(p != NULL);

    if (p -> GamutCheck)
        cmsPipelineFree(ContextID, p -> GamutCheck);

    if (p -> Lut)
        cmsPipelineFree(ContextID, p -> Lut);

    if (p ->InputColorant)
        cmsFreeNamedColorList(ContextID, p ->InputColorant);

    if (p -> OutputColorant)
        cmsFreeNamedColorList(ContextID, p ->OutputColorant);

    if (p ->Sequence)
        cmsFreeProfileSequenceDescription(ContextID, p ->Sequence);

    if (p ->UserData)
        p ->FreeUserData(ContextID, p ->UserData);

    _cmsFree(ContextID, (void *) p);
}

// Apply transform.
void CMSEXPORT cmsDoTransform(cmsContext ContextID, cmsHTRANSFORM  Transform,
                              const void* InputBuffer,
                              void* OutputBuffer,
                              cmsUInt32Number Size)

{
    _cmsTRANSFORM* p = (_cmsTRANSFORM*) Transform;
    cmsStride stride;

    stride.BytesPerLineIn = 0;  // Not used
    stride.BytesPerLineOut = 0;
    stride.BytesPerPlaneIn = Size;
    stride.BytesPerPlaneOut = Size;

    p -> xform(ContextID, p, InputBuffer, OutputBuffer, Size, 1, &stride);
}


// This is a legacy stride for planar
void CMSEXPORT cmsDoTransformStride(cmsContext ContextID, cmsHTRANSFORM  Transform,
                              const void* InputBuffer,
                              void* OutputBuffer,
                              cmsUInt32Number Size, cmsUInt32Number Stride)

{
    _cmsTRANSFORM* p = (_cmsTRANSFORM*) Transform;
    cmsStride stride;

    stride.BytesPerLineIn = 0;
    stride.BytesPerLineOut = 0;
    stride.BytesPerPlaneIn = Stride;
    stride.BytesPerPlaneOut = Stride;

    p -> xform(ContextID, p, InputBuffer, OutputBuffer, Size, 1, &stride);
}

// This is the "fast" function for plugins
void CMSEXPORT cmsDoTransformLineStride(cmsContext ContextID, cmsHTRANSFORM  Transform,
                              const void* InputBuffer,
                              void* OutputBuffer,
                              cmsUInt32Number PixelsPerLine,
                              cmsUInt32Number LineCount,
                              cmsUInt32Number BytesPerLineIn,
                              cmsUInt32Number BytesPerLineOut,
                              cmsUInt32Number BytesPerPlaneIn,
                              cmsUInt32Number BytesPerPlaneOut)

{
    _cmsTRANSFORM* p = (_cmsTRANSFORM*) Transform;
    cmsStride stride;

    stride.BytesPerLineIn = BytesPerLineIn;
    stride.BytesPerLineOut = BytesPerLineOut;
    stride.BytesPerPlaneIn = BytesPerPlaneIn;
    stride.BytesPerPlaneOut = BytesPerPlaneOut;

    p->xform(ContextID, p, InputBuffer, OutputBuffer, PixelsPerLine, LineCount, &stride);
}



// Transform routines ----------------------------------------------------------------------------------------------------------

// Float xform converts floats. Since there are no performance issues, one routine does all job, including gamut check.
// Note that because extended range, we can use a -1.0 value for out of gamut in this case.
static
void FloatXFORM(cmsContext ContextID, _cmsTRANSFORM* p,
                const void* in,
                void* out,
                cmsUInt32Number PixelsPerLine,
                cmsUInt32Number LineCount,
                const cmsStride* Stride)
{
    cmsUInt8Number* accum;
    cmsUInt8Number* output;
    cmsFloat32Number fIn[cmsMAXCHANNELS], fOut[cmsMAXCHANNELS];
    cmsFloat32Number OutOfGamut;
    cmsUInt32Number i, j, c, strideIn, strideOut;

    _cmsHandleExtraChannels(ContextID, p, in, out, PixelsPerLine, LineCount, Stride);

    strideIn = 0;
    strideOut = 0;
    memset(fIn, 0, sizeof(fIn));
    memset(fOut, 0, sizeof(fIn));

    for (i = 0; i < LineCount; i++) {

        accum = (cmsUInt8Number*)in + strideIn;
        output = (cmsUInt8Number*)out + strideOut;

        for (j = 0; j < PixelsPerLine; j++) {

            accum = p->FromInputFloat(ContextID, p, fIn, accum, Stride->BytesPerPlaneIn);

            // Any gamut chack to do?
            if (p->GamutCheck != NULL) {

                // Evaluate gamut marker.
                cmsPipelineEvalFloat(ContextID, fIn, &OutOfGamut, p->GamutCheck);

                // Is current color out of gamut?
                if (OutOfGamut > 0.0) {

                    // Certainly, out of gamut
                    for (c = 0; c < cmsMAXCHANNELS; c++)
                        fOut[c] = -1.0;

                }
                else {
                    // No, proceed normally
                    cmsPipelineEvalFloat(ContextID, fIn, fOut, p->Lut);
                }
            }
            else {

                // No gamut check at all
                cmsPipelineEvalFloat(ContextID, fIn, fOut, p->Lut);
            }


            output = p->ToOutputFloat(ContextID, p, fOut, output, Stride->BytesPerPlaneOut);
        }

        strideIn += Stride->BytesPerLineIn;
        strideOut += Stride->BytesPerLineOut;
    }

}


static
void NullFloatXFORM(cmsContext ContextID, _cmsTRANSFORM* p,
                    const void* in,
                    void* out,
                    cmsUInt32Number PixelsPerLine,
                    cmsUInt32Number LineCount,
                    const cmsStride* Stride)

{
    cmsUInt8Number* accum;
    cmsUInt8Number* output;
    cmsFloat32Number fIn[cmsMAXCHANNELS];
    cmsUInt32Number i, j, strideIn, strideOut;

    _cmsHandleExtraChannels(ContextID, p, in, out, PixelsPerLine, LineCount, Stride);

    strideIn = 0;
    strideOut = 0;
    memset(fIn, 0, sizeof(fIn));

    for (i = 0; i < LineCount; i++) {

           accum = (cmsUInt8Number*) in + strideIn;
           output = (cmsUInt8Number*) out + strideOut;

           for (j = 0; j < PixelsPerLine; j++) {

                  accum = p->FromInputFloat(ContextID, p, fIn, accum, Stride ->BytesPerPlaneIn);
                  output = p->ToOutputFloat(ContextID, p, fIn, output, Stride->BytesPerPlaneOut);
           }

           strideIn += Stride->BytesPerLineIn;
           strideOut += Stride->BytesPerLineOut;
    }
}

// 16 bit precision -----------------------------------------------------------------------------------------------------------

// Null transformation, only applies formatters. No cach�
static
void NullXFORM(cmsContext ContextID, _cmsTRANSFORM* p,
               const void* in,
               void* out,
               cmsUInt32Number PixelsPerLine,
               cmsUInt32Number LineCount,
               const cmsStride* Stride)
{
    cmsUInt8Number* accum;
    cmsUInt8Number* output;
    cmsUInt16Number wIn[cmsMAXCHANNELS];
    cmsUInt32Number i, j, strideIn, strideOut;

    _cmsHandleExtraChannels(ContextID, p, in, out, PixelsPerLine, LineCount, Stride);

    strideIn = 0;
    strideOut = 0;
    memset(wIn, 0, sizeof(wIn));

    for (i = 0; i < LineCount; i++) {

           accum = (cmsUInt8Number*)in + strideIn;
           output = (cmsUInt8Number*)out + strideOut;

           for (j = 0; j < PixelsPerLine; j++) {

                  accum = p->FromInput(ContextID, p, wIn, accum, Stride->BytesPerPlaneIn);
                  output = p->ToOutput(ContextID, p, wIn, output, Stride->BytesPerPlaneOut);
    }

           strideIn += Stride->BytesPerLineIn;
           strideOut += Stride->BytesPerLineOut;
    }

}


// No gamut check, no cache, 16 bits
#define FUNCTION_NAME PrecalculatedXFORM
#include "extra_xform.h"


// Auxiliary: Handle precalculated gamut check. The retrieval of context may be alittle bit slow, but this function is not critical.
static
void TransformOnePixelWithGamutCheck(cmsContext ContextID, _cmsTRANSFORM* p,
                                     const cmsUInt16Number wIn[],
                                     cmsUInt16Number wOut[])
{
    cmsUInt16Number wOutOfGamut;

    p ->GamutCheck ->Eval16Fn(ContextID, wIn, &wOutOfGamut, p ->GamutCheck ->Data);
    if (wOutOfGamut >= 1) {

        cmsUInt16Number i;
        _cmsAlarmCodesChunkType* ContextAlarmCodes = (_cmsAlarmCodesChunkType*) _cmsContextGetClientChunk(ContextID, AlarmCodesContext);

        for (i=0; i < p ->Lut->OutputChannels; i++) {

            wOut[i] = ContextAlarmCodes ->AlarmCodes[i];
        }
    }
    else
        p ->Lut ->Eval16Fn(ContextID, wIn, wOut, p -> Lut->Data);
}

// Gamut check, No cach�, 16 bits.
#define FUNCTION_NAME PrecalculatedXFORMGamutCheck
#define GAMUTCHECK
#include "extra_xform.h"

// No gamut check, Cach�, 16 bits,
#define FUNCTION_NAME CachedXFORM
#define CACHED
#include "extra_xform.h"

// All those nice features together
#define FUNCTION_NAME CachedXFORMGamutCheck
#define CACHED
#define GAMUTCHECK
#include "extra_xform.h"

// No gamut check, Cache, 16 bits, <= 4 bytes
#define FUNCTION_NAME CachedXFORM4
#define CACHED
#define INBYTES 4
#define EXTRABYTES 0
#include "extra_xform.h"

// No gamut check, Cache, 16 bits, <= 8 bytes total
#define FUNCTION_NAME CachedXFORM8
#define CACHED
#define INBYTES 8
#define EXTRABYTES 0
#include "extra_xform.h"

// Special ones for common cases.
#define FUNCTION_NAME CachedXFORM3to1
#define CACHED
#define INBYTES 6
#define EXTRABYTES 0
#define UNPACK(CTX,T,D,S,Z)                               \
do {                                                      \
       (D)[0] = FROM_8_TO_16(*(S)); (S)++;     /* R */    \
       (D)[1] = FROM_8_TO_16(*(S)); (S)++;     /* G */    \
       (D)[2] = FROM_8_TO_16(*(S)); (S)++;     /* B */    \
} while (0)
#define PACK(CTX,T,S,D,Z)        \
do {                             \
    *(D)++ = FROM_16_TO_8(*(S)); \
} while (0);
#include "extra_xform.h"

#define FUNCTION_NAME CachedXFORM3x2to1x2
#define CACHED
#define INBYTES 6
#define EXTRABYTES 0
#define UNPACK(CTX,T,D,S,Z)                                 \
do {                                                        \
       (D)[0] = *(cmsUInt16Number *)(S); (S) += 2; /* R */  \
       (D)[1] = *(cmsUInt16Number *)(S); (S) += 2; /* G */  \
       (D)[2] = *(cmsUInt16Number *)(S); (S) += 2; /* B */  \
} while (0)
#define PACK(CTX,T,S,D,Z)                        \
do {                                             \
    *(cmsUInt16Number *)(D) = *(S); (D) += 2;    \
} while (0);
#include "extra_xform.h"

// Transform plug-ins ----------------------------------------------------------------------------------------------------

// List of used-defined transform factories
typedef struct _cmsTransformCollection_st {

    _cmsTransform2Factory  Factory;
    cmsBool                OldXform;   // Factory returns xform function in the old style

    struct _cmsTransformCollection_st *Next;

} _cmsTransformCollection;

// The linked list head
_cmsTransformPluginChunkType _cmsTransformPluginChunk = { NULL };


// Duplicates the zone of memory used by the plug-in in the new context
static
void DupPluginTransformList(struct _cmsContext_struct* ctx,
                                               const struct _cmsContext_struct* src)
{
   _cmsTransformPluginChunkType newHead = { NULL };
   _cmsTransformCollection*  entry;
   _cmsTransformCollection*  Anterior = NULL;
   _cmsTransformPluginChunkType* head = (_cmsTransformPluginChunkType*) src->chunks[TransformPlugin];

    // Walk the list copying all nodes
   for (entry = head->TransformCollection;
        entry != NULL;
        entry = entry ->Next) {

            _cmsTransformCollection *newEntry = ( _cmsTransformCollection *) _cmsSubAllocDup(ctx ->MemPool, entry, sizeof(_cmsTransformCollection));

            if (newEntry == NULL)
                return;

            // We want to keep the linked list order, so this is a little bit tricky
            newEntry -> Next = NULL;
            if (Anterior)
                Anterior -> Next = newEntry;

            Anterior = newEntry;

            if (newHead.TransformCollection == NULL)
                newHead.TransformCollection = newEntry;
    }

  ctx ->chunks[TransformPlugin] = _cmsSubAllocDup(ctx->MemPool, &newHead, sizeof(_cmsTransformPluginChunkType));
}

// Allocates memory for transform plugin factory
void _cmsAllocTransformPluginChunk(struct _cmsContext_struct* ctx,
                                        const struct _cmsContext_struct* src)
{
    if (src != NULL) {

        // Copy all linked list
        DupPluginTransformList(ctx, src);
    }
    else {
        static _cmsTransformPluginChunkType TransformPluginChunkType = { NULL };
        ctx ->chunks[TransformPlugin] = _cmsSubAllocDup(ctx ->MemPool, &TransformPluginChunkType, sizeof(_cmsTransformPluginChunkType));
    }
}

// Adaptor for old versions of plug-in
static
void _cmsTransform2toTransformAdaptor(cmsContext ContextID, struct _cmstransform_struct *CMMcargo,
                                      const void* InputBuffer,
                                      void* OutputBuffer,
                                      cmsUInt32Number PixelsPerLine,
                                      cmsUInt32Number LineCount,
                                      const cmsStride* Stride)
{

       cmsUInt32Number i, strideIn, strideOut;

       _cmsHandleExtraChannels(ContextID, CMMcargo, InputBuffer, OutputBuffer, PixelsPerLine, LineCount, Stride);

       strideIn = 0;
       strideOut = 0;

       for (i = 0; i < LineCount; i++) {

              void *accum = (cmsUInt8Number*)InputBuffer + strideIn;
              void *output = (cmsUInt8Number*)OutputBuffer + strideOut;

              CMMcargo->OldXform(ContextID, CMMcargo, accum, output, PixelsPerLine, Stride->BytesPerPlaneIn);

              strideIn += Stride->BytesPerLineIn;
              strideOut += Stride->BytesPerLineOut;
       }
}



// Register new ways to transform
cmsBool  _cmsRegisterTransformPlugin(cmsContext ContextID, cmsPluginBase* Data)
{
    cmsPluginTransform* Plugin = (cmsPluginTransform*) Data;
    _cmsTransformCollection* fl;
    _cmsTransformPluginChunkType* ctx = ( _cmsTransformPluginChunkType*) _cmsContextGetClientChunk(ContextID,TransformPlugin);

    if (Data == NULL) {

        // Free the chain. Memory is safely freed at exit
        ctx->TransformCollection = NULL;
        return TRUE;
    }

    // Factory callback is required
    if (Plugin->factories.xform == NULL) return FALSE;


    fl = (_cmsTransformCollection*) _cmsPluginMalloc(ContextID, sizeof(_cmsTransformCollection));
    if (fl == NULL) return FALSE;

    // Check for full xform plug-ins previous to 2.8, we would need an adapter in that case
    if (Plugin->base.ExpectedVersion < 2080) {

           fl->OldXform = TRUE;
    }
    else
           fl->OldXform = FALSE;

    // Copy the parameters
    fl->Factory = Plugin->factories.xform;

    // Keep linked list
    fl ->Next = ctx->TransformCollection;
    ctx->TransformCollection = fl;

    // All is ok
    return TRUE;
}


void CMSEXPORT _cmsSetTransformUserData(struct _cmstransform_struct *CMMcargo, void* ptr, _cmsFreeUserDataFn FreePrivateDataFn)
{
    _cmsAssert(CMMcargo != NULL);
    CMMcargo ->UserData = ptr;
    CMMcargo ->FreeUserData = FreePrivateDataFn;
}

// returns the pointer defined by the plug-in to store private data
void * CMSEXPORT _cmsGetTransformUserData(struct _cmstransform_struct *CMMcargo)
{
    _cmsAssert(CMMcargo != NULL);
    return CMMcargo ->UserData;
}

// returns the current formatters
void CMSEXPORT _cmsGetTransformFormatters16(struct _cmstransform_struct *CMMcargo, cmsFormatter16* FromInput, cmsFormatter16* ToOutput)
{
     _cmsAssert(CMMcargo != NULL);
     if (FromInput) *FromInput = CMMcargo ->FromInput;
     if (ToOutput)  *ToOutput  = CMMcargo ->ToOutput;
}

void CMSEXPORT _cmsGetTransformFormattersFloat(struct _cmstransform_struct *CMMcargo, cmsFormatterFloat* FromInput, cmsFormatterFloat* ToOutput)
{
     _cmsAssert(CMMcargo != NULL);
     if (FromInput) *FromInput = CMMcargo ->FromInputFloat;
     if (ToOutput)  *ToOutput  = CMMcargo ->ToOutputFloat;
}


void
_cmsFindFormatter(_cmsTRANSFORM* p, cmsUInt32Number InputFormat, cmsUInt32Number OutputFormat, cmsUInt32Number dwFlags)
{
    if (dwFlags & cmsFLAGS_NULLTRANSFORM)
        p ->xform = NullXFORM;
    else if (dwFlags & cmsFLAGS_NOCACHE) {
        if (dwFlags & cmsFLAGS_GAMUTCHECK)
            p ->xform = PrecalculatedXFORMGamutCheck;  // Gamut check, no cach<E9>
        else
            p ->xform = PrecalculatedXFORM;  // No cach<E9>, no gamut check
    } else if (dwFlags & cmsFLAGS_GAMUTCHECK)
        p ->xform = CachedXFORMGamutCheck;    // Gamut check, cach<E9>
    else if (T_EXTRA(InputFormat) != 0)
        p ->xform = CachedXFORM;  // No gamut check, cach<E9>
    else if ((InputFormat & ~COLORSPACE_SH(31)) == (CHANNELS_SH(3)|BYTES_SH(1)) &&
             (OutputFormat & ~COLORSPACE_SH(31)) == (CHANNELS_SH(1)|BYTES_SH(1)))
        p ->xform = CachedXFORM3to1;
    else if ((InputFormat & ~COLORSPACE_SH(31)) == (CHANNELS_SH(3)|BYTES_SH(2)) &&
             (OutputFormat & ~COLORSPACE_SH(31)) == (CHANNELS_SH(1)|BYTES_SH(2)))
        p ->xform = CachedXFORM3x2to1x2;
    else {
        int inwords = T_CHANNELS(InputFormat);
        if (inwords <= 2)
            p ->xform = CachedXFORM4;
        else if (inwords <= 4)
            p ->xform = CachedXFORM8;
        else
            p ->xform = CachedXFORM;  // No gamut check, cach<E9>
    }
}

// Allocate transform struct and set it to defaults. Ask the optimization plug-in about if those formats are proper
// for separated transforms. If this is the case,
static
_cmsTRANSFORM* AllocEmptyTransform(cmsContext ContextID, cmsPipeline* lut,
                                               cmsUInt32Number Intent, cmsUInt32Number* InputFormat, cmsUInt32Number* OutputFormat, cmsUInt32Number* dwFlags)
{
     _cmsTransformPluginChunkType* ctx = ( _cmsTransformPluginChunkType*) _cmsContextGetClientChunk(ContextID, TransformPlugin);
     _cmsTransformCollection* Plugin;

       // Allocate needed memory
       _cmsTRANSFORM* p = (_cmsTRANSFORM*)_cmsMallocZero(ContextID, sizeof(_cmsTRANSFORM));
       if (!p) {
              cmsPipelineFree(ContextID, lut);
              return NULL;
       }

       // Store the proposed pipeline
       p->Lut = lut;

       // Let's see if any plug-in want to do the transform by itself
       if (p->Lut != NULL) {

              for (Plugin = ctx->TransformCollection;
                     Plugin != NULL;
                     Plugin = Plugin->Next) {

                     if (Plugin->Factory(ContextID, &p->xform, &p->UserData, &p->FreeUserData, &p->Lut, InputFormat, OutputFormat, dwFlags)) {

                            // Last plugin in the declaration order takes control. We just keep
                            // the original parameters as a logging.
                            // Note that cmsFLAGS_CAN_CHANGE_FORMATTER is not set, so by default
                            // an optimized transform is not reusable. The plug-in can, however, change
                            // the flags and make it suitable.

                            p->InputFormat = *InputFormat;
                            p->OutputFormat = *OutputFormat;
                            p->dwOriginalFlags = *dwFlags;

                            // Fill the formatters just in case the optimized routine is interested.
                            // No error is thrown if the formatter doesn't exist. It is up to the optimization
                            // factory to decide what to do in those cases.
                            p->FromInput = _cmsGetFormatter(ContextID, *InputFormat, cmsFormatterInput, CMS_PACK_FLAGS_16BITS).Fmt16;
                            p->ToOutput = _cmsGetFormatter(ContextID, *OutputFormat, cmsFormatterOutput, CMS_PACK_FLAGS_16BITS).Fmt16;
                            p->FromInputFloat = _cmsGetFormatter(ContextID, *InputFormat, cmsFormatterInput, CMS_PACK_FLAGS_FLOAT).FmtFloat;
                            p->ToOutputFloat = _cmsGetFormatter(ContextID, *OutputFormat, cmsFormatterOutput, CMS_PACK_FLAGS_FLOAT).FmtFloat;

                            // Save the day? (Ignore the warning)
                            if (Plugin->OldXform) {
                                   p->OldXform = (_cmsTransformFn) p->xform;
                                   p->xform = _cmsTransform2toTransformAdaptor;
                            }
                            return p;
                     }
              }

              // Not suitable for the transform plug-in, let's check  the pipeline plug-in
              _cmsOptimizePipeline(ContextID, &p->Lut, Intent, InputFormat, OutputFormat, dwFlags);
       }

    // Check whatever this is a true floating point transform
    if (_cmsFormatterIsFloat(*InputFormat) && _cmsFormatterIsFloat(*OutputFormat)) {

        // Get formatter function always return a valid union, but the contents of this union may be NULL.
        p ->FromInputFloat = _cmsGetFormatter(ContextID, *InputFormat,  cmsFormatterInput, CMS_PACK_FLAGS_FLOAT).FmtFloat;
        p ->ToOutputFloat  = _cmsGetFormatter(ContextID, *OutputFormat, cmsFormatterOutput, CMS_PACK_FLAGS_FLOAT).FmtFloat;
        *dwFlags |= cmsFLAGS_CAN_CHANGE_FORMATTER;

        if (p ->FromInputFloat == NULL || p ->ToOutputFloat == NULL) {

            cmsSignalError(ContextID, cmsERROR_UNKNOWN_EXTENSION, "Unsupported raster format");
            cmsDeleteTransform(ContextID, p);
            return NULL;
        }

        if (*dwFlags & cmsFLAGS_NULLTRANSFORM) {

            p ->xform = NullFloatXFORM;
        }
        else {
            // Float transforms don't use cach�, always are non-NULL
            p ->xform = FloatXFORM;
        }

    }
    else {

        if (*InputFormat == 0 && *OutputFormat == 0) {
            p ->FromInput = p ->ToOutput = NULL;
            *dwFlags |= cmsFLAGS_CAN_CHANGE_FORMATTER;
        }
        else {

            cmsUInt32Number BytesPerPixelInput;

            p ->FromInput = _cmsGetFormatter(ContextID, *InputFormat,  cmsFormatterInput, CMS_PACK_FLAGS_16BITS).Fmt16;
            p ->ToOutput  = _cmsGetFormatter(ContextID, *OutputFormat, cmsFormatterOutput, CMS_PACK_FLAGS_16BITS).Fmt16;

            if (p ->FromInput == NULL || p ->ToOutput == NULL) {

                cmsSignalError(ContextID, cmsERROR_UNKNOWN_EXTENSION, "Unsupported raster format");
                cmsDeleteTransform(ContextID, p);
                return NULL;
            }

            BytesPerPixelInput = T_BYTES(p ->InputFormat);
            if (BytesPerPixelInput == 0 || BytesPerPixelInput >= 2)
                   *dwFlags |= cmsFLAGS_CAN_CHANGE_FORMATTER;

        }

        _cmsFindFormatter(p, *InputFormat, *OutputFormat, *dwFlags);
    }

    p ->InputFormat     = *InputFormat;
    p ->OutputFormat    = *OutputFormat;
    p ->dwOriginalFlags = *dwFlags;
    p ->UserData        = NULL;
    return p;
}

static
cmsBool GetXFormColorSpaces(cmsContext ContextID, cmsUInt32Number nProfiles, cmsHPROFILE hProfiles[], cmsColorSpaceSignature* Input, cmsColorSpaceSignature* Output)
{
    cmsColorSpaceSignature ColorSpaceIn, ColorSpaceOut;
    cmsColorSpaceSignature PostColorSpace;
    cmsUInt32Number i;

    if (nProfiles == 0) return FALSE;
    if (hProfiles[0] == NULL) return FALSE;

    *Input = PostColorSpace = cmsGetColorSpace(ContextID, hProfiles[0]);

    for (i=0; i < nProfiles; i++) {

        cmsProfileClassSignature cls;
        cmsHPROFILE hProfile = hProfiles[i];

        int lIsInput = (PostColorSpace != cmsSigXYZData) &&
                       (PostColorSpace != cmsSigLabData);

        if (hProfile == NULL) return FALSE;

        cls = cmsGetDeviceClass(ContextID, hProfile);

        if (cls == cmsSigNamedColorClass) {

            ColorSpaceIn    = cmsSig1colorData;
            ColorSpaceOut   = (nProfiles > 1) ? cmsGetPCS(ContextID, hProfile) : cmsGetColorSpace(ContextID, hProfile);
        }
        else
        if (lIsInput || (cls == cmsSigLinkClass)) {

            ColorSpaceIn    = cmsGetColorSpace(ContextID, hProfile);
            ColorSpaceOut   = cmsGetPCS(ContextID, hProfile);
        }
        else
        {
            ColorSpaceIn    = cmsGetPCS(ContextID, hProfile);
            ColorSpaceOut   = cmsGetColorSpace(ContextID, hProfile);
        }

        if (i==0)
            *Input = ColorSpaceIn;

        PostColorSpace = ColorSpaceOut;
    }

    *Output = PostColorSpace;

    return TRUE;
}

// Check colorspace
static
cmsBool  IsProperColorSpace(cmsContext ContextID, cmsColorSpaceSignature Check, cmsUInt32Number dwFormat)
{
    int Space1 = (int) T_COLORSPACE(dwFormat);
    int Space2 = _cmsLCMScolorSpace(ContextID, Check);

    if (Space1 == PT_ANY) return TRUE;
    if (Space1 == Space2) return TRUE;

    if (Space1 == PT_LabV2 && Space2 == PT_Lab) return TRUE;
    if (Space1 == PT_Lab   && Space2 == PT_LabV2) return TRUE;

    return FALSE;
}

// ----------------------------------------------------------------------------------------------------------------

// Jun-21-2000: Some profiles (those that comes with W2K) comes
// with the media white (media black?) x 100. Add a sanity check

static
void NormalizeXYZ(cmsContext ContextID, cmsCIEXYZ* Dest)
{
    cmsUNUSED_PARAMETER(ContextID);
    while (Dest -> X > 2. &&
           Dest -> Y > 2. &&
           Dest -> Z > 2.) {

               Dest -> X /= 10.;
               Dest -> Y /= 10.;
               Dest -> Z /= 10.;
       }
}

static
void SetWhitePoint(cmsContext ContextID, cmsCIEXYZ* wtPt, const cmsCIEXYZ* src)
{
    if (src == NULL) {
        wtPt ->X = cmsD50X;
        wtPt ->Y = cmsD50Y;
        wtPt ->Z = cmsD50Z;
    }
    else {
        wtPt ->X = src->X;
        wtPt ->Y = src->Y;
        wtPt ->Z = src->Z;

        NormalizeXYZ(ContextID, wtPt);
    }

}

// New to lcms 2.0 -- have all parameters available.
cmsHTRANSFORM CMSEXPORT cmsCreateExtendedTransform(cmsContext ContextID,
                                                   cmsUInt32Number nProfiles, cmsHPROFILE hProfiles[],
                                                   cmsBool  BPC[],
                                                   cmsUInt32Number Intents[],
                                                   cmsFloat64Number AdaptationStates[],
                                                   cmsHPROFILE hGamutProfile,
                                                   cmsUInt32Number nGamutPCSposition,
                                                   cmsUInt32Number InputFormat,
                                                   cmsUInt32Number OutputFormat,
                                                   cmsUInt32Number dwFlags)
{
    _cmsTRANSFORM* xform;
    cmsColorSpaceSignature EntryColorSpace;
    cmsColorSpaceSignature ExitColorSpace;
    cmsPipeline* Lut;
    cmsUInt32Number LastIntent = Intents[nProfiles-1];

    // If it is a fake transform
    if (dwFlags & cmsFLAGS_NULLTRANSFORM)
    {
        return AllocEmptyTransform(ContextID, NULL, INTENT_PERCEPTUAL, &InputFormat, &OutputFormat, &dwFlags);
    }

    // If gamut check is requested, make sure we have a gamut profile
    if (dwFlags & cmsFLAGS_GAMUTCHECK) {
        if (hGamutProfile == NULL) dwFlags &= ~cmsFLAGS_GAMUTCHECK;
    }

    // On floating point transforms, inhibit cache
    if (_cmsFormatterIsFloat(InputFormat) || _cmsFormatterIsFloat(OutputFormat))
        dwFlags |= cmsFLAGS_NOCACHE;

    // Mark entry/exit spaces
    if (!GetXFormColorSpaces(ContextID, nProfiles, hProfiles, &EntryColorSpace, &ExitColorSpace)) {
        cmsSignalError(ContextID, cmsERROR_NULL, "NULL input profiles on transform");
        return NULL;
    }

    // Check if proper colorspaces
    if (!IsProperColorSpace(ContextID, EntryColorSpace, InputFormat)) {
        cmsSignalError(ContextID, cmsERROR_COLORSPACE_CHECK, "Wrong input color space on transform");
        return NULL;
    }

    if (!IsProperColorSpace(ContextID, ExitColorSpace, OutputFormat)) {
        cmsSignalError(ContextID, cmsERROR_COLORSPACE_CHECK, "Wrong output color space on transform");
        return NULL;
    }

    // Create a pipeline with all transformations
    Lut = _cmsLinkProfiles(ContextID, nProfiles, Intents, hProfiles, BPC, AdaptationStates, dwFlags);
    if (Lut == NULL) {
        cmsSignalError(ContextID, cmsERROR_NOT_SUITABLE, "Couldn't link the profiles");
        return NULL;
    }

    // Check channel count
    if ((cmsChannelsOf(ContextID, EntryColorSpace) != cmsPipelineInputChannels(ContextID, Lut)) ||
        (cmsChannelsOf(ContextID, ExitColorSpace)  != cmsPipelineOutputChannels(ContextID, Lut))) {
        cmsPipelineFree(ContextID, Lut);
        cmsSignalError(ContextID, cmsERROR_NOT_SUITABLE, "Channel count doesn't match. Profile is corrupted");
        return NULL;
    }


    // All seems ok
    xform = AllocEmptyTransform(ContextID, Lut, LastIntent, &InputFormat, &OutputFormat, &dwFlags);
    if (xform == NULL) {
        return NULL;
    }

    // Keep values
    xform ->EntryColorSpace = EntryColorSpace;
    xform ->ExitColorSpace  = ExitColorSpace;
    xform ->RenderingIntent = Intents[nProfiles-1];

    // Take white points
    SetWhitePoint(ContextID, &xform->EntryWhitePoint, (cmsCIEXYZ*) cmsReadTag(ContextID, hProfiles[0], cmsSigMediaWhitePointTag));
    SetWhitePoint(ContextID, &xform->ExitWhitePoint,  (cmsCIEXYZ*) cmsReadTag(ContextID, hProfiles[nProfiles-1], cmsSigMediaWhitePointTag));


    // Create a gamut check LUT if requested
    if (hGamutProfile != NULL && (dwFlags & cmsFLAGS_GAMUTCHECK))
        xform ->GamutCheck  = _cmsCreateGamutCheckPipeline(ContextID, hProfiles,
                                                        BPC, Intents,
                                                        AdaptationStates,
                                                        nGamutPCSposition,
                                                        hGamutProfile);


    // Try to read input and output colorant table
    if (cmsIsTag(ContextID, hProfiles[0], cmsSigColorantTableTag)) {

        // Input table can only come in this way.
        xform ->InputColorant = cmsDupNamedColorList(ContextID, (cmsNAMEDCOLORLIST*) cmsReadTag(ContextID, hProfiles[0], cmsSigColorantTableTag));
    }

    // Output is a little bit more complex.
    if (cmsGetDeviceClass(ContextID, hProfiles[nProfiles-1]) == cmsSigLinkClass) {

        // This tag may exist only on devicelink profiles.
        if (cmsIsTag(ContextID, hProfiles[nProfiles-1], cmsSigColorantTableOutTag)) {

            // It may be NULL if error
            xform ->OutputColorant = cmsDupNamedColorList(ContextID, (cmsNAMEDCOLORLIST*) cmsReadTag(ContextID, hProfiles[nProfiles-1], cmsSigColorantTableOutTag));
        }

    } else {

        if (cmsIsTag(ContextID, hProfiles[nProfiles-1], cmsSigColorantTableTag)) {

            xform -> OutputColorant = cmsDupNamedColorList(ContextID, (cmsNAMEDCOLORLIST*) cmsReadTag(ContextID, hProfiles[nProfiles-1], cmsSigColorantTableTag));
        }
    }

    // Store the sequence of profiles
    if (dwFlags & cmsFLAGS_KEEP_SEQUENCE) {
        xform ->Sequence = _cmsCompileProfileSequence(ContextID, nProfiles, hProfiles);
    }
    else
        xform ->Sequence = NULL;

    // If this is a cached transform, init first value, which is zero (16 bits only)
    if (!(dwFlags & cmsFLAGS_NOCACHE)) {

        memset(&xform ->Cache.CacheIn, 0, sizeof(xform ->Cache.CacheIn));

        if (xform ->GamutCheck != NULL) {
            TransformOnePixelWithGamutCheck(ContextID, xform, xform ->Cache.CacheIn, xform->Cache.CacheOut);
        }
        else {

            xform ->Lut ->Eval16Fn(ContextID, xform ->Cache.CacheIn, xform->Cache.CacheOut, xform -> Lut->Data);
        }

    }

    return (cmsHTRANSFORM) xform;
}

// Multiprofile transforms: Gamut check is not available here, as it is unclear from which profile the gamut comes.
cmsHTRANSFORM CMSEXPORT cmsCreateMultiprofileTransformTHR(cmsContext ContextID,
                                                       cmsHPROFILE hProfiles[],
                                                       cmsUInt32Number nProfiles,
                                                       cmsUInt32Number InputFormat,
                                                       cmsUInt32Number OutputFormat,
                                                       cmsUInt32Number Intent,
                                                       cmsUInt32Number dwFlags)
{
    cmsUInt32Number i;
    cmsBool BPC[256];
    cmsUInt32Number Intents[256];
    cmsFloat64Number AdaptationStates[256];

    if (nProfiles <= 0 || nProfiles > 255) {
         cmsSignalError(ContextID, cmsERROR_RANGE, "Wrong number of profiles. 1..255 expected, %d found.", nProfiles);
        return NULL;
    }

    for (i=0; i < nProfiles; i++) {
        BPC[i] = dwFlags & cmsFLAGS_BLACKPOINTCOMPENSATION ? TRUE : FALSE;
        Intents[i] = Intent;
        AdaptationStates[i] = cmsSetAdaptationStateTHR(ContextID, -1);
    }


    return cmsCreateExtendedTransform(ContextID, nProfiles, hProfiles, BPC, Intents, AdaptationStates, NULL, 0, InputFormat, OutputFormat, dwFlags);
}



cmsHTRANSFORM CMSEXPORT cmsCreateMultiprofileTransform(cmsHPROFILE hProfiles[],
                                                  cmsUInt32Number nProfiles,
                                                  cmsUInt32Number InputFormat,
                                                  cmsUInt32Number OutputFormat,
                                                  cmsUInt32Number Intent,
                                                  cmsUInt32Number dwFlags)
{

    if (nProfiles <= 0 || nProfiles > 255) {
         cmsSignalError(NULL, cmsERROR_RANGE, "Wrong number of profiles. 1..255 expected, %d found.", nProfiles);
         return NULL;
    }

    return cmsCreateMultiprofileTransformTHR(NULL, hProfiles,
                                                  nProfiles,
                                                  InputFormat,
                                                  OutputFormat,
                                                  Intent,
                                                  dwFlags);
}

cmsHTRANSFORM CMSEXPORT cmsCreateTransformTHR(cmsContext ContextID,
                                              cmsHPROFILE Input,
                                              cmsUInt32Number InputFormat,
                                              cmsHPROFILE Output,
                                              cmsUInt32Number OutputFormat,
                                              cmsUInt32Number Intent,
                                              cmsUInt32Number dwFlags)
{

    cmsHPROFILE hArray[2];

    hArray[0] = Input;
    hArray[1] = Output;

    return cmsCreateMultiprofileTransformTHR(ContextID, hArray, Output == NULL ? 1U : 2U, InputFormat, OutputFormat, Intent, dwFlags);
}

CMSAPI cmsHTRANSFORM CMSEXPORT cmsCreateTransform(cmsHPROFILE Input,
                                                  cmsUInt32Number InputFormat,
                                                  cmsHPROFILE Output,
                                                  cmsUInt32Number OutputFormat,
                                                  cmsUInt32Number Intent,
                                                  cmsUInt32Number dwFlags)
{
    return cmsCreateTransformTHR(NULL, Input, InputFormat, Output, OutputFormat, Intent, dwFlags);
}


cmsHTRANSFORM CMSEXPORT cmsCreateProofingTransformTHR(cmsContext ContextID,
                                                   cmsHPROFILE InputProfile,
                                                   cmsUInt32Number InputFormat,
                                                   cmsHPROFILE OutputProfile,
                                                   cmsUInt32Number OutputFormat,
                                                   cmsHPROFILE ProofingProfile,
                                                   cmsUInt32Number nIntent,
                                                   cmsUInt32Number ProofingIntent,
                                                   cmsUInt32Number dwFlags)
{
    cmsHPROFILE hArray[4];
    cmsUInt32Number Intents[4];
    cmsBool  BPC[4];
    cmsFloat64Number Adaptation[4];
    cmsBool  DoBPC = (dwFlags & cmsFLAGS_BLACKPOINTCOMPENSATION) ? TRUE : FALSE;


    hArray[0]  = InputProfile; hArray[1] = ProofingProfile; hArray[2]  = ProofingProfile;               hArray[3] = OutputProfile;
    Intents[0] = nIntent;      Intents[1] = nIntent;        Intents[2] = INTENT_RELATIVE_COLORIMETRIC;  Intents[3] = ProofingIntent;
    BPC[0]     = DoBPC;        BPC[1] = DoBPC;              BPC[2] = 0;                                 BPC[3] = 0;

    Adaptation[0] = Adaptation[1] = Adaptation[2] = Adaptation[3] = cmsSetAdaptationStateTHR(ContextID, -1);

    if (!(dwFlags & (cmsFLAGS_SOFTPROOFING|cmsFLAGS_GAMUTCHECK)))
        return cmsCreateTransformTHR(ContextID, InputProfile, InputFormat, OutputProfile, OutputFormat, nIntent, dwFlags);

    return cmsCreateExtendedTransform(ContextID, 4, hArray, BPC, Intents, Adaptation,
                                        ProofingProfile, 1, InputFormat, OutputFormat, dwFlags);

}


cmsHTRANSFORM CMSEXPORT cmsCreateProofingTransform(cmsHPROFILE InputProfile,
                                                   cmsUInt32Number InputFormat,
                                                   cmsHPROFILE OutputProfile,
                                                   cmsUInt32Number OutputFormat,
                                                   cmsHPROFILE ProofingProfile,
                                                   cmsUInt32Number nIntent,
                                                   cmsUInt32Number ProofingIntent,
                                                   cmsUInt32Number dwFlags)
{
    return cmsCreateProofingTransformTHR(NULL,
                                                   InputProfile,
                                                   InputFormat,
                                                   OutputProfile,
                                                   OutputFormat,
                                                   ProofingProfile,
                                                   nIntent,
                                                   ProofingIntent,
                                                   dwFlags);
}


// Grab the input/output formats
cmsUInt32Number CMSEXPORT cmsGetTransformInputFormat(cmsContext ContextID, cmsHTRANSFORM hTransform)
{
    _cmsTRANSFORM* xform = (_cmsTRANSFORM*) hTransform;
    cmsUNUSED_PARAMETER(ContextID);

    if (xform == NULL) return 0;
    return xform->InputFormat;
}

cmsUInt32Number CMSEXPORT cmsGetTransformOutputFormat(cmsContext ContextID, cmsHTRANSFORM hTransform)
{
    _cmsTRANSFORM* xform = (_cmsTRANSFORM*) hTransform;
    cmsUNUSED_PARAMETER(ContextID);

    if (xform == NULL) return 0;
    return xform->OutputFormat;
}

// For backwards compatibility
cmsBool CMSEXPORT cmsChangeBuffersFormat(cmsContext ContextID, cmsHTRANSFORM hTransform,
                                         cmsUInt32Number InputFormat,
                                         cmsUInt32Number OutputFormat)
{
    _cmsTRANSFORM* xform = (_cmsTRANSFORM*) hTransform;
    cmsFormatter16 FromInput, ToOutput;


    // We only can afford to change formatters if previous transform is at least 16 bits
    if (!(xform ->dwOriginalFlags & cmsFLAGS_CAN_CHANGE_FORMATTER)) {

        cmsSignalError(ContextID, cmsERROR_NOT_SUITABLE, "cmsChangeBuffersFormat works only on transforms created originally with at least 16 bits of precision");
        return FALSE;
    }

    FromInput = _cmsGetFormatter(ContextID, InputFormat,  cmsFormatterInput, CMS_PACK_FLAGS_16BITS).Fmt16;
    ToOutput  = _cmsGetFormatter(ContextID, OutputFormat, cmsFormatterOutput, CMS_PACK_FLAGS_16BITS).Fmt16;

    if (FromInput == NULL || ToOutput == NULL) {

        cmsSignalError(ContextID, cmsERROR_UNKNOWN_EXTENSION, "Unsupported raster format");
        return FALSE;
    }

    xform ->InputFormat  = InputFormat;
    xform ->OutputFormat = OutputFormat;
    xform ->FromInput    = FromInput;
    xform ->ToOutput     = ToOutput;
    return TRUE;
}
