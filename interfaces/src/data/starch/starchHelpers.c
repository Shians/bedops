//=========
// Author:  Alex Reynolds & Shane Neph
// Project: starch
// File:    starchHelpers.c
//=========

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <bzlib.h>
#include <zlib.h>
#include <assert.h>

#include "data/starch/starchSha1Digest.h"
#include "data/starch/starchBase64Coding.h"
#include "data/starch/starchHelpers.h"
#include "data/starch/starchConstants.h"
#include "data/starch/starchFileHelpers.h"
#include "suite/BEDOPS.Constants.hpp"

#ifdef __cplusplus
using namespace Bed;
#endif

int 
STARCH_compressFileWithGzip(const char *inFn, char **outFn, off_t *outFnSize)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_compressFileWithGzip() ---\n");
#endif
    FILE *inFnPtr = NULL;
    FILE *outFnPtr = NULL;    
    struct stat outSt;

    /* create output file handle */
    *outFn = (char *) malloc((strlen(inFn) + 4) * sizeof(**outFn)); /* 4 <- ".gz\0" */
    if (! *outFn) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return STARCH_FATAL_ERROR;
    }
    sprintf(*outFn, "%s.gz", inFn);
    outFnPtr = STARCH_fopen(*outFn, "wb");
    if (!outFnPtr) {
        fprintf(stderr, "ERROR: Could not open a gzip output file handle to %s\n", *outFn);
        return STARCH_FATAL_ERROR;
    }

    /* open input for compression */
    inFnPtr = STARCH_fopen(inFn, "r");
    if (!inFnPtr) {
        fprintf(stderr, "ERROR: Could not open a gzip input file handle to %s\n", inFn);
        return STARCH_FATAL_ERROR;
    }

    /* compress file */
    /* cf. http://www.zlib.net/manual.html for level information */
    STARCH_gzip_deflate(inFnPtr, outFnPtr, STARCH_Z_COMPRESSION_LEVEL);

    /* close file pointers */
    fclose(inFnPtr);
    fclose(outFnPtr);

    /* get gzip file size */
    if (stat((const char *)*outFn, &outSt) != 0) {
        fprintf(stderr, "ERROR: Could not get gzip file attributes\n");
        return STARCH_FATAL_ERROR;
    }
    *outFnSize = outSt.st_size;

    return 0;
}

int 
STARCH_compressFileWithBzip2(const char *inFn, char **outFn, off_t *outFnSize)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_compressFileWithBzip2() ---\n");
#endif
    FILE *inFnPtr = NULL;
    FILE *outFnPtr = NULL;
    BZFILE *bzFp = NULL;
    int nBzBuf = STARCH_BZ_BUFFER_MAX_LENGTH;
    char bzBuf[STARCH_BZ_BUFFER_MAX_LENGTH];
    int bzError;
    int c;
    unsigned int idx = 0U;
    struct stat outSt;

    /* create output file handle */
    *outFn = (char *) malloc((strlen(inFn) + 5) * sizeof(**outFn)); /* 5 <- ".bz2\0" */
    if (! *outFn) {
        fprintf(stderr, "ERROR: Out of memory\n");
        return STARCH_FATAL_ERROR;
    }
    sprintf(*outFn, "%s.bz2", inFn);
    outFnPtr = STARCH_fopen(*outFn, "wb");
    if (!outFnPtr) {
        fprintf(stderr, "ERROR: Could not open a bzip2 output file handle to %s\n", *outFn);
        return STARCH_FATAL_ERROR;
    }

    /* open input for compression */
    inFnPtr = STARCH_fopen(inFn, "r");
    if (!inFnPtr) {
        fprintf(stderr, "ERROR: Could not open a bzip2 input file handle to %s\n", inFn);
        return STARCH_FATAL_ERROR;
    }
    bzFp = BZ2_bzWriteOpen( &bzError, outFnPtr, STARCH_BZ_COMPRESSION_LEVEL, 0, 0 );
    if (bzError != BZ_OK) {
        BZ2_bzWriteClose ( &bzError, bzFp, 0, NULL, NULL );
        fprintf(stderr, "ERROR: Could not open bzip2 file handle\n");
        return STARCH_FATAL_ERROR;
    }

    /* compress to bz stream */
    while ((c = fgetc(inFnPtr)) != EOF) { 
        bzBuf[idx++] = (char) c;
        if (idx == STARCH_BZ_BUFFER_MAX_LENGTH) {
            BZ2_bzWrite( &bzError, bzFp, bzBuf, nBzBuf );
            if (bzError == BZ_IO_ERROR) {
                BZ2_bzWriteClose ( &bzError, bzFp, 0, NULL, NULL );
                fprintf(stderr, "ERROR: Could not write to bzip2 file handle\n");
                return STARCH_FATAL_ERROR;
            }
            idx = 0;
        }
    }
    /* write out remainder of bzip2 buffer to output */
    bzBuf[idx] = '\0';
    BZ2_bzWrite(&bzError, bzFp, bzBuf, (int) idx);
    if (bzError == BZ_IO_ERROR) {   
        BZ2_bzWriteClose ( &bzError, bzFp, 0, NULL, NULL );
        fprintf(stderr, "ERROR: Could not write to bzip2 file handle\n");
        return STARCH_FATAL_ERROR;
    }

    /* close bzip2 stream */
    BZ2_bzWriteClose( &bzError, bzFp, 0, NULL, NULL );
    if (bzError == BZ_IO_ERROR) {
        fprintf(stderr, "ERROR: Could not close bzip2 file handle\n");
        return STARCH_FATAL_ERROR;
    }

    /* close input */
    fclose(inFnPtr);

    /* close output */
    fclose(outFnPtr);

    /* get bzip2 file size */
    if (stat((const char *)*outFn, &outSt) != 0) {
        fprintf(stderr, "ERROR: Could not get bzip2 file attributes\n");
        return STARCH_FATAL_ERROR;
    }
    *outFnSize = outSt.st_size;

    return 0;
}

int 
STARCH_createTransformTokens(const char *s, const char delim, char **chr, int64_t *start, int64_t *stop, char **remainder, BedLineType *lineType) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_createTransformTokens() ---\n");
#endif
    unsigned int charCnt, sCnt, elemCnt;
    char buffer[STARCH_BUFFER_MAX_LENGTH];
    unsigned int idIdx = 0U;
    unsigned int restIdx = 0U;

    charCnt = 0U;
    sCnt = 0U;
    elemCnt = 0U;

    do {
        buffer[charCnt++] = s[sCnt];
        if ((s[sCnt] == delim) || (s[sCnt] == '\0')) {
            if (elemCnt < 3) {
                buffer[(charCnt - 1)] = '\0';
                charCnt = 0;
            }
            switch (elemCnt) {
                case 0: {
                    /* we do field validation tests after we determine what kind of BED line this is */
                    /* copy element to chromosome variable, if memory is available */
                    *chr = (char *) malloc((strlen(buffer) + 1) * sizeof(**chr));
                    if (! *chr) {
                        fprintf(stderr, "ERROR: Ran out of memory while creating transform tokens\n");
                        return STARCH_FATAL_ERROR;
                    }
                    strncpy(*chr, (const char *)buffer, strlen(buffer) + 1);                    
                    break;
                }
                case 1: {
                    /* test if element string is longer than allowed bounds */
                    if (strlen(buffer) > MAX_DEC_INTEGERS) {
                        fprintf(stderr, "ERROR: Start coordinate field length is too long ([%s] must be no greater than %ld characters)\n", buffer, MAX_DEC_INTEGERS);
                        return STARCH_FATAL_ERROR;
                    }
                    /* convert element string to start coordinate */
                    *start = (int64_t) strtoull((const char *)buffer, NULL, STARCH_RADIX);
                    /* test if start coordinate is larger than allowed bounds */
                    if (*start > (int64_t) MAX_COORD_VALUE) {
                        fprintf(stderr, "ERROR: Start coordinate field value (%" PRId64 ") is too great (must be less than %" PRId64 ")\n", *start, (int64_t) MAX_COORD_VALUE);
                        return STARCH_FATAL_ERROR;
                    }
                    break;
                }
                case 2: {
                    /* test if element string is longer than allowed bounds */
                    if (strlen(buffer) > MAX_DEC_INTEGERS) {
                        fprintf(stderr, "ERROR: Stop coordinate field length is too long (must be no greater than %ld characters)\n", MAX_DEC_INTEGERS);
                        return STARCH_FATAL_ERROR;
                    }
                    /* convert element string to stop coordinate */
                    *stop = (int64_t) strtoull((const char *)buffer, NULL, STARCH_RADIX);
                    /* test if stop coordinate is larger than allowed bounds */
                    if (*stop > (int64_t) MAX_COORD_VALUE) {
                        fprintf(stderr, "ERROR: Stop coordinate field value (%" PRId64 ") is too great (must be less than %" PRId64 ")\n", *stop, (int64_t) MAX_COORD_VALUE);
                        return STARCH_FATAL_ERROR;
                    }
                    break;
                }
                /* just keep filling the buffer until we reach s[sCnt]'s null -- we do tests later */
                case 3:                    
                    break;
            }

            /* determine what type of BED input line we're working with */
            if (strncmp((const char *) *chr, kStarchBedHeaderTrack, strlen(kStarchBedHeaderTrack)) == 0) {
                *lineType = kBedLineHeaderTrack;
                elemCnt = 3;
            }
            else if (strncmp((const char *) *chr, kStarchBedHeaderBrowser, strlen(kStarchBedHeaderBrowser)) == 0) {
                *lineType = kBedLineHeaderBrowser;
                elemCnt = 3;
            }
            else if (strncmp((const char *) *chr, kStarchBedHeaderSAM, strlen(kStarchBedHeaderSAM)) == 0) {
                *lineType = kBedLineHeaderSAM;
                elemCnt = 3;
            }
            else if (strncmp((const char *) *chr, kStarchBedHeaderVCF, strlen(kStarchBedHeaderVCF)) == 0) {
                *lineType = kBedLineHeaderVCF;
                elemCnt = 3;
            }
            else if (strncmp((const char *) *chr, kStarchBedGenericComment, strlen(kStarchBedGenericComment)) == 0) {
                *lineType = kBedLineGenericComment;
                elemCnt = 3;
            }
            else {
                *lineType = kBedLineCoordinates;
                elemCnt++;
            }
            
            /* if line type is of kBedLineCoordinates type, then we test chromosome length */
            if (*lineType == kBedLineCoordinates) {
                if (strlen(*chr) > Bed::TOKEN_CHR_MAX_LENGTH) {
                    fprintf(stderr, "ERROR: Chromosome field length is too long (must be no longer than %ld characters)\n", TOKEN_CHR_MAX_LENGTH);
                    return STARCH_FATAL_ERROR;
                }
            }
            /* otherwise, we limit the length of a comment line to TOKENS_HEADER_MAX_LENGTH */
            else {
                if (strlen(*chr) > Bed::TOKENS_HEADER_MAX_LENGTH) {
                    fprintf(stderr, "ERROR: Comment line length is too long (must be no longer than %ld characters)\n", TOKEN_CHR_MAX_LENGTH);
                    return STARCH_FATAL_ERROR;
                }                
            }
        }
    } while (s[sCnt++] != '\0');

    if (elemCnt > 3) {
        buffer[(charCnt - 1)] = '\0';
        /* test id field length */
        while ((buffer[idIdx] != delim) && (idIdx++ < Bed::TOKEN_ID_MAX_LENGTH)) {}
        if (idIdx == Bed::TOKEN_ID_MAX_LENGTH) {
            fprintf(stderr, "ERROR: Id field is too long (must be less than %ld characters long)\n", Bed::TOKEN_ID_MAX_LENGTH);
            return STARCH_FATAL_ERROR;
        }
        /* test remnant of buffer, if there is more to look at */
        if (charCnt > idIdx) {
            while ((buffer[idIdx++] != '\0') && (restIdx++ < Bed::TOKEN_REST_MAX_LENGTH)) {}
            if (restIdx == Bed::TOKEN_REST_MAX_LENGTH) {
                fprintf(stderr, "ERROR: Remainder of BED input after id field is too long (must be less than %ld characters long)\n", Bed::TOKEN_REST_MAX_LENGTH);
                return STARCH_FATAL_ERROR;
            }
        }
        *remainder = (char *) malloc((strlen(buffer) + 1) * sizeof(**remainder));
        if (! *remainder) {
            fprintf(stderr, "ERROR: Ran out of memory handling token remainder\n");
            return STARCH_FATAL_ERROR;
        }
        strncpy(*remainder, (const char *)buffer, strlen(buffer) + 1);        
    }

    return 0;
}

int 
STARCH_createTransformTokensForHeaderlessInput(const char *s, const char delim, char **chr, int64_t *start, int64_t *stop, char **remainder) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_createTransformTokensForHeaderlessInput() ---\n");
#endif
    unsigned int charCnt, sCnt, elemCnt;
    char buffer[STARCH_BUFFER_MAX_LENGTH];
    char *chrCopy = NULL;
    char *remainderCopy = NULL;
    unsigned int idIdx = 0U;
    unsigned int restIdx = 0U;

    charCnt = 0U;
    sCnt = 0U;
    elemCnt = 0U;

    do {
        buffer[charCnt++] = s[sCnt];
        if ((s[sCnt] == delim) || (s[sCnt] == '\0')) {
            if (elemCnt < 3) {
                buffer[(charCnt - 1)] = '\0';
                charCnt = 0;
            }
            switch (elemCnt) {
                case 0: {
#ifdef DEBUG
                    fprintf(stderr, "\tcase 0\n");
#endif
                    /* test if element string is longer than allowed bounds */
                    if (strlen(buffer) > Bed::TOKEN_CHR_MAX_LENGTH) {
                        fprintf(stderr, "ERROR: Chromosome field length is too long (must be no longer than %ld characters)\n", Bed::TOKEN_CHR_MAX_LENGTH);
                        return STARCH_FATAL_ERROR;
                    }
                    /* copy element to chromosome variable, if memory is available */
                    if (! *chr)
                        *chr = (char *) malloc((strlen(buffer) + 1) * sizeof(**chr));
                    else if (strlen(buffer) > strlen(*chr)) {
                        chrCopy = (char *) realloc(*chr, strlen(buffer) * 2);
                        if (!chrCopy) {
                            fprintf(stderr, "ERROR: Ran out of memory while extending chr token\n");
                            return STARCH_FATAL_ERROR;
                        }
                        *chr = chrCopy;
                    }
                    if (! *chr) {
                        fprintf(stderr, "ERROR: Ran out of memory while creating transform tokens\n");
                        return STARCH_FATAL_ERROR;
                    }
                    strncpy(*chr, (const char *)buffer, strlen(buffer) + 1);                    
                    break;
                }
                case 1: {
#ifdef DEBUG
                    fprintf(stderr, "\tcase 1\n");
#endif
                    /* test if element string is longer than allowed bounds */
                    if (strlen(buffer) > Bed::MAX_DEC_INTEGERS) {
                        fprintf(stderr, "ERROR: Start coordinate field length is too long ([%s] must be no greater than %ld characters)\n", buffer, Bed::MAX_DEC_INTEGERS);
                        return STARCH_FATAL_ERROR;
                    }
                    /* convert element string to start coordinate */
                    *start = (int64_t) strtoll((const char *)buffer, NULL, STARCH_RADIX);
                    /* test if start coordinate is larger than allowed bounds */
                    if (*start > (int64_t) MAX_COORD_VALUE) {
                        fprintf(stderr, "ERROR: Start coordinate field value (%" PRId64 ") is too great (must be less than %" PRId64 ")\n", *start, (int64_t) Bed::MAX_COORD_VALUE);
                        return STARCH_FATAL_ERROR;
                    }
                    break;
                }
                case 2: {
#ifdef DEBUG
                    fprintf(stderr, "\tcase 2\n");
#endif
                    /* test if element string is longer than allowed bounds */
                    if (strlen(buffer) > Bed::MAX_DEC_INTEGERS) {
                        fprintf(stderr, "ERROR: Stop coordinate field length is too long (must be no greater than %ld characters)\n", Bed::MAX_DEC_INTEGERS);
                        return STARCH_FATAL_ERROR;
                    }
                    /* convert element string to stop coordinate */
                    *stop = (int64_t) strtoll((const char *)buffer, NULL, STARCH_RADIX);
                    /* test if stop coordinate is larger than allowed bounds */
                    if (*stop > (int64_t) Bed::MAX_COORD_VALUE) {
                        fprintf(stderr, "ERROR: Stop coordinate field value (%" PRId64 ") is too great (must be less than %" PRId64 ")\n", *stop, (int64_t) Bed::MAX_COORD_VALUE);
                        return STARCH_FATAL_ERROR;
                    }
                    break;
                }
                /* just keep filling the buffer until we reach s[sCnt]'s null -- we do tests later */
                case 3: {
#ifdef DEBUG
                    fprintf(stderr, "\tcase 3\n");
#endif
                    break;
                }
            }
            elemCnt++;
        }
    } while (s[sCnt++] != '\0');

    /* apply tests on id and score-strand-... ("rest") element strings */
    if (elemCnt > 3) {
        buffer[(charCnt - 1)] = '\0';
        /* test id field length */
        while ((buffer[idIdx] != delim) && (idIdx++ < Bed::TOKEN_ID_MAX_LENGTH)) {}
        if (idIdx == Bed::TOKEN_ID_MAX_LENGTH) {
            fprintf(stderr, "ERROR: Id field is too long (must be less than %ld characters long)\n", Bed::TOKEN_ID_MAX_LENGTH);
            return STARCH_FATAL_ERROR;
        }
        /* test remnant ("rest") of buffer, if there is more to look at */
        if (charCnt > idIdx) {
            while ((buffer[idIdx++] != '\0') && (restIdx++ < Bed::TOKEN_REST_MAX_LENGTH)) {}
            if (restIdx == Bed::TOKEN_REST_MAX_LENGTH) {
                fprintf(stderr, "ERROR: Remainder of BED input after id field is too long (must be less than %ld characters long)\n", Bed::TOKEN_REST_MAX_LENGTH);
                return STARCH_FATAL_ERROR;
            }
        }
        /* resize remainder, if needed */
        if (! *remainder)
            *remainder = (char *) malloc((strlen(buffer) + 1) * sizeof(**remainder));
        else if (strlen(buffer) > strlen(*remainder)) {
#ifdef DEBUG
            fprintf(stderr, "\tresizing remainder...\n");
#endif
            remainderCopy = (char *) realloc(*remainder, strlen(buffer) * 2);
            if (!remainderCopy) {
                fprintf(stderr, "ERROR: Ran out of memory extending remainder token\n");
                return STARCH_FATAL_ERROR;
            }
            *remainder = remainderCopy;
        }
        if (! *remainder) {
            fprintf(stderr, "ERROR: Ran out of memory handling remainder token\n");
            return STARCH_FATAL_ERROR;
        }
        strncpy(*remainder, (const char *)buffer, strlen(buffer) + 1);
    }
    else if (elemCnt < 2) {
        fprintf(stderr, "ERROR: BED data is missing chromosome and/or coordinate data\n");
        return STARCH_FATAL_ERROR;
    }

#ifdef DEBUG
    fprintf(stderr, "\t (post create-transform-tokens: chr -> %s\n\tstart -> %" PRId64 "\n\tstop -> %" PRId64 "\n\tremainder -> %s\n", *chr, *start, *stop, *remainder);
#endif

    return 0;
}

int 
STARCH_transformInput(Metadata **md, const FILE *fp, const CompressionType type, const char *tag, const char *note) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_transformInput() ---\n");
#endif
    int c;
    int cIdx = 0;
    int recIdx = 0;
    char buffer[STARCH_BUFFER_MAX_LENGTH];
    char *outFn = NULL;
    FILE *outFnPtr = NULL;
    FILE *streamPtr = (FILE *) fp;
    char *outCompressedFn = NULL;
    char *remainder = NULL;
    char *prevChromosome = NULL;
    char *chromosome = NULL;
    Metadata *firstRecord = NULL;
    int64_t start = 0LL;
    int64_t stop = 0LL;
    int64_t previousStop = 0LL;
    int64_t lastPosition = 0LL;
    int64_t lcDiff = 0LL;
    int64_t coordDiff = 0ULL;
    uint64_t outFnSize = 0ULL;
    Boolean withinChr = kStarchFalse;
    unsigned long lineIdx = 0UL;
    int64_t outCompressedFnSize = 0;
    char *legacyMdBuf = NULL; 
    char *dynamicMdBuf = NULL;
    BedLineType lineType = kBedLineTypeUndefined;
    char nonCoordLineBuf[STARCH_BUFFER_MAX_LENGTH] = {0};
    Boolean nonCoordLineBufNeedsPrinting = kStarchFalse;
    Bed::BaseCountType totalNonUniqueBases = 0UL;
    Bed::BaseCountType totalUniqueBases = 0UL;

    if (!streamPtr)
        streamPtr = stdin;

    while ((c = fgetc(streamPtr)) != EOF) {
        buffer[cIdx] = (char)c;
        if (c == '\n') {
            lineIdx++;
            buffer[cIdx] = '\0';
            if (STARCH_createTransformTokens(buffer, '\t', &chromosome, &start, &stop, &remainder, &lineType) == 0) 
            {
                /* 
                   Either previous chromosome is NULL, or current chromosome does 
                   not equal previous chromosome, but the line must be of the 
                   type 'kBedLineCoordinates' (cf. 'starchMetadataHelpers.h')
                */

                if ( (lineType == kBedLineCoordinates) && ((!prevChromosome) || (strcmp(chromosome, prevChromosome) != 0)) ) 
                {
                    /* close old output file pointer */
                    if (outFnPtr != NULL) {
                        fclose(outFnPtr); 
                        outFnPtr = NULL;

                        if (type == kBzip2) {
                            /* bzip-compress the previous file */
                            if (STARCH_compressFileWithBzip2((const char *)outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                                fprintf(stderr, "ERROR: Could not bzip2 compress per-chromosome output file %s\n", outFn);
                                return STARCH_FATAL_ERROR;
                            }
                        }
                        else if (type == kGzip) {
                            /* gzip-compress file */
                            if (STARCH_compressFileWithGzip((const char*) outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                                fprintf(stderr, "ERROR: Could not gzip compress per-chromosome output file %s\n", outFn);
                                return STARCH_FATAL_ERROR;
                            }
                        }
                        else {
                            fprintf(stderr, "ERROR: Unknown compression regime\n");
                            return STARCH_FATAL_ERROR;
                        }
                        /* delete uncompressed file */
                        if (remove(outFn) != 0) {
                            fprintf(stderr, "ERROR: Could not delete per-chromosome output file %s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }

                        /* update metadata with compressed file attributes */
                        if (STARCH_updateMetadataForChromosome(md, 
                                                               prevChromosome, 
                                                               outCompressedFn, 
                                                               (uint64_t) outCompressedFnSize, 
                                                               lineIdx, 
                                                               totalNonUniqueBases, 
                                                               totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                            fprintf(stderr, "ERROR: Could not update metadata%s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }

                        /* cleanup */
                        free(outCompressedFn); outCompressedFn = NULL;
                    }

		    /* test if current chromosome is already a Metadata record */
		    if (STARCH_chromosomeInMetadataRecords((const Metadata *)*md, chromosome) == STARCH_EXIT_SUCCESS) {
		        fprintf(stderr, "ERROR: Found same chromosome in earlier portion of file. Possible interleaving issue? Be sure to first sort input with sort-bed or remove --do-not-sort option from conversion script.\n");
                        return STARCH_FATAL_ERROR;
		    }

                    /* open new output file pointer */
                    if (!outFnPtr) {
                        outFn = (char *) malloc(strlen(chromosome) + strlen(tag) + 2);
                        sprintf(outFn, "%s.%s", chromosome, tag);
                        outFnPtr = STARCH_fopen(outFn, "a");
                        if (!outFnPtr) {
                            fprintf(stderr, "ERROR: Could not open an intermediate output file handle to %s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }
                    }
                    else {
                        fprintf(stderr, "ERROR: Could not open per-chromosome output file\n");
                        return STARCH_FATAL_ERROR;
                    }

                    /* add chromosome to metadata */
                    if (recIdx == 0) {
                        *md = STARCH_createMetadata(chromosome, 
                                                    outFn, 
                                                    outFnSize, 
                                                    lineIdx, 
                                                    totalNonUniqueBases, 
                                                    totalUniqueBases);
                        firstRecord = *md;
                    }
                    else {
                        *md = STARCH_addMetadata(*md, 
                                                 chromosome, 
                                                 outFn, 
                                                 outFnSize, 
                                                 lineIdx, 
                                                 totalNonUniqueBases, 
                                                 totalUniqueBases);
                    }

                    /* make previous chromosome the current chromosome */
                    if (prevChromosome != NULL) {
                        free(prevChromosome);
                        prevChromosome = NULL;
                    }
                    prevChromosome = (char *) malloc(strlen(chromosome) + 1);
                    strncpy(prevChromosome, (const char *)chromosome, strlen(chromosome) + 1);

                    /* reset flag, lastPosition and lcDiff, increment record index */
                    withinChr = kStarchFalse;
                    lastPosition = 0LL;
                    previousStop = 0LL;
                    lcDiff = 0LL;
                    lineIdx = 0UL;
                    totalNonUniqueBases = 0UL;
                    totalUniqueBases = 0UL;
                    recIdx++;
                }
                else if (lineType == kBedLineCoordinates) {
                    withinChr = kStarchFalse;
                }

                /* transform data, depending on line type */                

                if (lineType != kBedLineCoordinates) {
                    /* 
                       It is possible for custom track header data to collect on two or
                       more consecutive lines. So we concatenate with any previously collected
                       header data, which will then be sent to the output file pointer 
                       at some point in the future as one big chunk...
    
                       Note that we do not expect that contiguous custom track header information 
                       will be larger than STARCH_BUFFER_MAX_LENGTH bytes. This might well prove
                       to be a dangerous assumption, but probably not, as 1 MB is a lot of custom 
                       track data in one contiguous block. This situation seems fairly unlikely.
                    */
                    strncat(nonCoordLineBuf, (const char *)chromosome, strlen(chromosome) + 1);
                    nonCoordLineBuf[strlen(nonCoordLineBuf)] = '\n';
                    nonCoordLineBufNeedsPrinting = kStarchTrue;
                }
                else {
                    if (nonCoordLineBufNeedsPrinting == kStarchTrue) {
                        /* 
                           if there's custom track data that needs printin', we do so now 
                           and reset the buffer and print flag
                        */
                        fprintf(outFnPtr, "%s", nonCoordLineBuf);
                        memset(nonCoordLineBuf, 0, strlen(nonCoordLineBuf));
                        nonCoordLineBufNeedsPrinting = kStarchFalse;
                    }
                    if (stop > start)
                        coordDiff = stop - start;
                    else {
                        fprintf(stderr, "ERROR: Bed data is corrupt at line %lu (stop: %" PRId64 ", start: %" PRId64 ")\n", lineIdx, stop, start);
                        return STARCH_FATAL_ERROR;
                    }
                    if (coordDiff != lcDiff) {
                        lcDiff = coordDiff;
                        fprintf( outFnPtr, "p%" PRId64 "\n", coordDiff );
                    }
                    if (lastPosition != 0) {
                        if (remainder)
                            fprintf( outFnPtr, "%" PRId64 "\t%s\n", (start - lastPosition), remainder );
                        else
                            fprintf( outFnPtr, "%" PRId64 "\n", (start - lastPosition) );
                    }
                    else {
                        if (remainder)
                            fprintf( outFnPtr, "%" PRId64 "\t%s\n", start, remainder );
                        else
                            fprintf( outFnPtr, "%" PRId64 "\n", start );
                    }
                    totalNonUniqueBases += (Bed::BaseCountType) (stop - start);
                    if (previousStop <= start)
                        totalUniqueBases += (Bed::BaseCountType) (stop - start);
                    else if (previousStop < stop)
                        totalUniqueBases += (Bed::BaseCountType) (stop - previousStop);
                    lastPosition = stop;
                    previousStop = (stop > previousStop) ? stop : previousStop;
                }

                /* cleanup unused data */
                if (withinChr == kStarchTrue) {
                    free(chromosome); chromosome = NULL;
                }
                if (remainder) {
                    free(remainder); remainder = NULL;
                }
                cIdx = 0;                
            }
            else {
                fprintf(stderr, "ERROR: Bed data could not be transformed\n");
                return STARCH_FATAL_ERROR;
            }
        }
        else
            cIdx++;
    }
    
    /* compress the remaining file */
    if (outFnPtr != NULL) {
        fclose(outFnPtr); outFnPtr = NULL;
        if (type == kBzip2) {
            if (STARCH_compressFileWithBzip2((const char *)outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                fprintf(stderr, "ERROR: Could not bzip2 compress per-chromosome output file %s\n", outFn);
                return STARCH_FATAL_ERROR;
            }
        }
        else if (type == kGzip) {
            /* gzip-compress file */
            if (STARCH_compressFileWithGzip((const char*)outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                fprintf(stderr, "ERROR: Could not gzip compress per-chromosome output file %s\n", outFn);
                return STARCH_FATAL_ERROR;
            }
        }
        else {
            fprintf(stderr, "ERROR: Unknown compression regime\n");
            return STARCH_FATAL_ERROR;
        }
        /* delete uncompressed file */
        if (remove(outFn) != 0) {
            fprintf(stderr, "ERROR: Could not delete per-chromosome output file %s -- is the input's first column sorted lexicographically?\n", outFn);
            return STARCH_FATAL_ERROR;
        }
        /* update metadata with compressed file attributes */
        lineIdx++;
        STARCH_updateMetadataForChromosome(md, 
                                           prevChromosome, 
                                           outCompressedFn, 
                                           (uint64_t) outCompressedFnSize, 
                                           lineIdx, 
                                           totalNonUniqueBases, 
                                           totalUniqueBases);
        free(outCompressedFn); outCompressedFn = NULL; 
    }

    /* reposition metadata pointer to first record */
    *md = firstRecord;

    /* write metadata header to buffer */
    /* and concatenate metadata header with compressed files */
    if ((STARCH_MAJOR_VERSION == 1) && (STARCH_MINOR_VERSION == 0) && (STARCH_REVISION_VERSION == 0)) {
        legacyMdBuf = (char *) malloc(STARCH_LEGACY_METADATA_SIZE + 1);
        if (legacyMdBuf != NULL) {
            if (STARCH_writeJSONMetadata((const Metadata *)*md, &legacyMdBuf, (CompressionType *) &type, kStarchFalse, (const char *) note) != STARCH_EXIT_SUCCESS) {
                fprintf(stderr, "ERROR: Could not write metadata to buffer\n");
                return STARCH_FATAL_ERROR;
            }
            if (STARCH_mergeMetadataWithCompressedFiles((const Metadata *)*md, legacyMdBuf) != STARCH_EXIT_SUCCESS) {
                fprintf(stderr, "ERROR: Could not merge metadata with compressed streams\n");
                return STARCH_FATAL_ERROR;
            }
            free(legacyMdBuf);
            legacyMdBuf = NULL;
        }
        else 
            return STARCH_FATAL_ERROR;
    }
    else {
        /* this is the custom header version of the parser, so we set headerFlag to TRUE */
        if (STARCH_writeJSONMetadata((const Metadata *)*md, &dynamicMdBuf, (CompressionType *) &type, kStarchTrue, (const char *) note) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not write metadata to buffer\n");
            return STARCH_FATAL_ERROR;
        }
        if (STARCH_mergeMetadataWithCompressedFiles((const Metadata *)*md, dynamicMdBuf) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not merge metadata with compressed streams\n");
            return STARCH_FATAL_ERROR;
        }
        if (dynamicMdBuf != NULL) {
            free(dynamicMdBuf);
            dynamicMdBuf = NULL;
        }
        else
            return STARCH_FATAL_ERROR;
    }

    /* remove compressed files */
    if (STARCH_deleteCompressedFiles((const Metadata *)*md) != STARCH_EXIT_SUCCESS) {
        fprintf(stderr, "ERROR: Could not delete compressed streams\n");
        return STARCH_FATAL_ERROR;
    }

    /* cleanup */
    free(prevChromosome);

    return 0;
}

int 
STARCH_transformHeaderlessInput(Metadata **md, const FILE *fp, const CompressionType type, const char *tag, const Boolean finalizeFlag, const char *note) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_transformHeaderlessInput() ---\n");
#endif
    int c;
    int cIdx = 0;
    int recIdx = 0;
    char buffer[STARCH_BUFFER_MAX_LENGTH];
    char *outFn = NULL;
    FILE *outFnPtr = NULL;
    FILE *streamPtr = (FILE *) fp;
    char *outCompressedFn = NULL;
    char *remainder = NULL;
    char *prevChromosome = NULL;
    char *chromosome = NULL;
    Metadata *firstRecord = NULL;
    int64_t start = 0LL;
    int64_t stop = 0LL;
    int64_t previousStop = 0LL;
    int64_t lastPosition = 0LL;
    int64_t lcDiff = 0LL;
    int64_t coordDiff = 0LL;
    uint64_t outFnSize = 0ULL;
    Boolean withinChr = kStarchFalse;
    unsigned long lineIdx = 0UL;
    int64_t outCompressedFnSize = 0;
    char *legacyMdBuf = NULL; 
    char *dynamicMdBuf = NULL;
    Bed::BaseCountType totalNonUniqueBases = 0;
    Bed::BaseCountType totalUniqueBases = 0;

    if (!streamPtr)
        streamPtr = stdin;

    while ((c = fgetc(streamPtr)) != EOF) {
        buffer[cIdx] = (char) c;
        if (c == '\n') {
            lineIdx++;
            buffer[cIdx] = '\0';
            if (STARCH_createTransformTokensForHeaderlessInput(buffer, '\t', &chromosome, &start, &stop, &remainder) == 0) 
            {
                /* 
                   Either previous chromosome is NULL, or current chromosome does 
                   not equal previous chromosome
                */

                if ( (!prevChromosome) || (strcmp(chromosome, prevChromosome) != 0) ) 
                {
                    /* close old output file pointer */
                    if (outFnPtr != NULL) {
                        fclose(outFnPtr); 
                        outFnPtr = NULL;

                        if (type == kBzip2) {
                            /* bzip-compress the previous file */
                            if (STARCH_compressFileWithBzip2((const char *)outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                                fprintf(stderr, "ERROR: Could not bzip2 compress per-chromosome output file %s\n", outFn);
                                return STARCH_FATAL_ERROR;
                            }
                        }
                        else if (type == kGzip) {
                            /* gzip-compress file */
                            if (STARCH_compressFileWithGzip((const char*) outFn, &outCompressedFn, (off_t *) &outCompressedFnSize ) != 0) {
                                fprintf(stderr, "ERROR: Could not gzip compress per-chromosome output file %s\n", outFn);
                                return STARCH_FATAL_ERROR;
                            }
                        }
                        else {
                            fprintf(stderr, "ERROR: Unknown compression regime\n");
                            return STARCH_FATAL_ERROR;
                        }
                        /* delete uncompressed file */
                        if (remove(outFn) != 0) {
                            fprintf(stderr, "ERROR: Could not delete per-chromosome output file %s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }

                        /* update metadata with compressed file attributes */
                        if (STARCH_updateMetadataForChromosome(md, 
                                                               prevChromosome, 
                                                               outCompressedFn, 
                                                               (uint64_t) outCompressedFnSize, 
                                                               lineIdx, 
                                                               totalNonUniqueBases, 
                                                               totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                            fprintf(stderr, "ERROR: Could not update metadata%s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }

                        /* cleanup */
                        free(outCompressedFn); outCompressedFn = NULL;
                    }

		    /* test if current chromosome is already a Metadata record */
		    if (STARCH_chromosomeInMetadataRecords((const Metadata *)*md, chromosome) == STARCH_EXIT_SUCCESS) {
		        fprintf(stderr, "ERROR: Found same chromosome in earlier portion of file. Possible interleaving issue? Be sure to first sort input with sort-bed or remove --do-not-sort option from conversion script.\n");
                        return STARCH_FATAL_ERROR;
		    }

                    /* open new output file pointer */
                    if (!outFnPtr) {
                        outFn = (char *) malloc(strlen(chromosome) + strlen(tag) + 2);
                        sprintf(outFn, "%s.%s", chromosome, tag);
                        outFnPtr = STARCH_fopen(outFn, "a");
                        if (!outFnPtr) {
                            fprintf(stderr, "ERROR: Could not open an intermediate output file handle to %s\n", outFn);
                            return STARCH_FATAL_ERROR;
                        }
                    }
                    else {
                        fprintf(stderr, "ERROR: Could not open per-chromosome output file\n");
                        return STARCH_FATAL_ERROR;
                    }

                    /* add chromosome to metadata */
                    if (! *md) {
                        *md = STARCH_createMetadata(chromosome, outFn, outFnSize, lineIdx, totalNonUniqueBases, totalUniqueBases);
                        firstRecord = *md;
                    }
                    else {
                        *md = STARCH_addMetadata(*md, chromosome, outFn, outFnSize, lineIdx, totalNonUniqueBases, totalUniqueBases);
                    }

                    /* make previous chromosome the current chromosome */
                    if (prevChromosome != NULL) {
                        free(prevChromosome);
                        prevChromosome = NULL;
                    }
                    prevChromosome = (char *) malloc(strlen(chromosome) + 1);
                    strncpy(prevChromosome, (const char *)chromosome, strlen(chromosome) + 1);

                    /* reset flag, lastPosition and lcDiff, increment record index */
                    withinChr = kStarchFalse;
                    lastPosition = 0LL;
                    previousStop = 0LL;
                    lcDiff = 0LL;
                    lineIdx = 0UL;
                    totalNonUniqueBases = 0UL;
                    totalUniqueBases = 0UL;
                    recIdx++;
                }
                else
                    withinChr = kStarchTrue;

                /* transform data */
                if (stop > start)
                    coordDiff = stop - start;
                else {
                    fprintf(stderr, "ERROR: BED data is corrupt at line %lu (stop: %" PRId64 ", start: %" PRId64 ")\n", lineIdx, stop, start);
                    return STARCH_FATAL_ERROR;
                }
                if (coordDiff != lcDiff) {
                    lcDiff = coordDiff;
#ifdef DEBUG
                    fprintf(stderr, "\tp%" PRId64 "\n", coordDiff);
#endif
                    fprintf(outFnPtr, "p%" PRId64 "\n", coordDiff );
                }
                if (lastPosition != 0) {
                    if (remainder) {
#ifdef DEBUG
                        fprintf(stderr, "\t%" PRId64 "\t%s\n", (start - lastPosition), remainder);
#endif
                        fprintf(outFnPtr, "%" PRId64 "\t%s\n", (start - lastPosition), remainder );
                    }
                    else {
#ifdef DEBUG
                        fprintf(stderr, "\t%" PRId64 "\n", (start - lastPosition));
#endif
                        fprintf(outFnPtr, "%" PRId64 "\n", (start - lastPosition) );
                    }
                }
                else {
                    if (remainder) {
#ifdef DEBUG
                        fprintf(stderr, "\t%" PRId64 "\t%s\n", start, remainder );
#endif
                        fprintf(outFnPtr, "%" PRId64 "\t%s\n", start, remainder );
                    }
                    else {
#ifdef DEBUG
                        fprintf(stderr, "\t%" PRId64 "\n", start );
#endif
                        fprintf(outFnPtr, "%" PRId64 "\n", start );
                    }
                }
                totalNonUniqueBases += (Bed::BaseCountType) (stop - start);
                if (previousStop <= start)
                    totalUniqueBases += (Bed::BaseCountType) (stop - start);
                else if (previousStop < stop)
                    totalUniqueBases += (Bed::BaseCountType) (stop - previousStop);
                lastPosition = stop;
                previousStop = (stop > previousStop) ? stop : previousStop;

                /* cleanup unused data */
                if (withinChr == kStarchTrue) 
                    free(chromosome), chromosome = NULL;
                if (remainder) 
                    free(remainder), remainder = NULL;
                cIdx = 0;                
            }
            else {
                fprintf(stderr, "ERROR: BED data could not be transformed\n");
                return STARCH_FATAL_ERROR;
            }
        }
        else
            cIdx++;
    }
    
    /* compress the remaining file */
    if (outFnPtr != NULL) {
        fclose(outFnPtr); 
        outFnPtr = NULL;
        
        if (type == kBzip2) {
            if (STARCH_compressFileWithBzip2((const char *)outFn, 
                                             &outCompressedFn, 
                                             (off_t *) &outCompressedFnSize) != 0) {
                fprintf(stderr, "ERROR: Could not bzip2 compress per-chromosome output file %s\n", outFn);
                return STARCH_FATAL_ERROR;
            }
        }
        else if (type == kGzip) {
            /* gzip-compress file */
            if (STARCH_compressFileWithGzip((const char*)outFn, 
                                            &outCompressedFn, 
                                            (off_t *) &outCompressedFnSize) != 0) {
                fprintf(stderr, "ERROR: Could not gzip compress per-chromosome output file %s\n", outFn);
                return STARCH_FATAL_ERROR;
            }
        }
        else {
            fprintf(stderr, "ERROR: Unknown compression regime\n");
            return STARCH_FATAL_ERROR;
        }

        /* delete uncompressed, transformed file */
        if (remove(outFn) != 0) {
            fprintf(stderr, "ERROR: Could not delete per-chromosome output file %s -- is the input's first column sorted lexicographically?\n", outFn);
            return STARCH_FATAL_ERROR;
        }

        /* update metadata with compressed file attributes */
        lineIdx++;
        STARCH_updateMetadataForChromosome(md, 
                                           prevChromosome, 
                                           outCompressedFn, 
                                           (uint64_t) outCompressedFnSize, 
                                           lineIdx, 
                                           totalNonUniqueBases, 
                                           totalUniqueBases);

        free(outCompressedFn); outCompressedFn = NULL; 
        free(outFn); outFn = NULL;
    }

    /* 
        We return early if we don't need to bundle up the starch archive 
        at this stage. We do this for the starchcat utility, for example,
        because we're probably in the middle of transforming multiple
        streams...
    */

    if (finalizeFlag == kStarchFalse)
        return 0;


    /*
        Otherwise, we wrap things up. In the future, this will go into its 
        own function for clarity...
    */

    /* reposition metadata pointer to first record */
    *md = firstRecord;

    /* write metadata header to buffer */
    /* concatenate metadata header with compressed files */
    if ((STARCH_MAJOR_VERSION == 1) && (STARCH_MINOR_VERSION == 0) && (STARCH_REVISION_VERSION == 0)) {
        legacyMdBuf = (char *) malloc(STARCH_LEGACY_METADATA_SIZE + 1);
        if (legacyMdBuf != NULL) {
            /* headerless input was not supported in this version, so it is set to FALSE */
            if (STARCH_writeJSONMetadata((const Metadata *)*md, &legacyMdBuf, (CompressionType *) &type, kStarchFalse, (const char *) note) != STARCH_EXIT_SUCCESS) {
                fprintf(stderr, "ERROR: Could not write metadata to buffer\n");
                return STARCH_FATAL_ERROR;
            }
            if (STARCH_mergeMetadataWithCompressedFiles((const Metadata *)*md, legacyMdBuf) != STARCH_EXIT_SUCCESS) {
                fprintf(stderr, "ERROR: Could not merge metadata with compressed streams\n");
                return STARCH_FATAL_ERROR;
            }
            free(legacyMdBuf);
            legacyMdBuf = NULL;
        }
        else 
            return STARCH_FATAL_ERROR;
    }
    else {
        /* headerless input means headerFlag is FALSE */
        if (STARCH_writeJSONMetadata((const Metadata *)*md, &dynamicMdBuf, (CompressionType *) &type, kStarchFalse, (const char *) note) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not write metadata to buffer\n");
            return STARCH_FATAL_ERROR;
        }
        if (STARCH_mergeMetadataWithCompressedFiles((const Metadata *)*md, dynamicMdBuf) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not merge metadata with compressed streams\n");
            return STARCH_FATAL_ERROR;
        }

        if (dynamicMdBuf != NULL) {
            free(dynamicMdBuf);
            dynamicMdBuf = NULL;
        }
        else
            return STARCH_FATAL_ERROR;
    }

    /* remove compressed files */
    if (STARCH_deleteCompressedFiles((const Metadata *)*md) != STARCH_EXIT_SUCCESS) {
        fprintf(stderr, "ERROR: Could not delete compressed streams\n");
        return STARCH_FATAL_ERROR;
    }

    /* cleanup */
    free(prevChromosome);

    return 0;
}

Boolean 
STARCH_fileExists(const char *fn) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_fileExists() ---\n");
#endif
    struct stat buf;
    int i = stat (fn, &buf);
    
    /* 
        Regarding 64-bit support
        cf. http://www.gnu.org/s/libc/manual/html_node/Reading-Attributes.html

        When the sources are compiled with _FILE_OFFSET_BITS == 64 this function is 
        available under the name stat and so transparently replaces the interface for 
        small files on 32-bit machines.
     */

    if (i == 0)
        return kStarchTrue;

    return kStarchFalse;
}

char * 
STARCH_strndup(const char *s, size_t n) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH_strndup() ---\n");
#endif
    char *result;
    size_t len = strlen(s);

    if (n < len)
        len = n;

    result = (char *) malloc(len + 1);
    if (!result)
        return NULL;    

    result[len] = '\0';
    return (char *) memcpy (result, s, len);
}

int 
STARCH2_transformInput(unsigned char **header, Metadata **md, const FILE *inFp, const CompressionType compressionType, const char *tag, const char *note, const Boolean headerFlag)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_transformInput() ---\n");
#endif
    /*
        Overview of Starch rev. 2
        ------------------------------------------------

        We reserve a 4-byte header at the front of the file. The header contains 
        the following data:

        * magic number - the magic number '[ca][5c][ad][e5]' identifies this 
                         as a Starch rev. 2-formatted file 

                         (4 bytes, constant)

        At the end of the file, we write 128 bytes:

        * offset       - a zero-padded 16-digit value marks the byte
                         into the file at which the archive's metadata 
                         starts (including the 4-byte header)

                         (16 bytes, calculated)

        * hash         - a SHA-1 hash of the metadata string, to validate
                         archive integrity

                         (20 bytes, calculated)

        * reserved     - we keep 92 bytes of space free, in case we need it
                         for future purposes

                         (92 bytes, zeros)

        Before these 128 bytes, the compressed, per-chromosome streams start, and 
        we then wrap up by writing the metadata at the end of the file, followed by 
        the footer.
    */

    if (STARCH2_initializeStarchHeader(header) != STARCH_EXIT_SUCCESS) {
        fprintf(stderr, "ERROR: Could not initialize archive header.\n");
        return STARCH_EXIT_FAILURE;
    }
    
    if (STARCH2_writeStarchHeaderToOutputFp(*header, stdout) != STARCH_EXIT_SUCCESS) {
        fprintf(stderr, "ERROR: Could not write archive header to output file pointer.\n");
        return STARCH_EXIT_FAILURE;
    }

    if (headerFlag == kStarchFalse) {
        if (STARCH2_transformHeaderlessBEDInput(inFp, md, compressionType, tag, note) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not write transformed/compressed data to output file pointer.\n");
            return STARCH_EXIT_FAILURE;
        }
    }
    else {
        if (STARCH2_transformHeaderedBEDInput(inFp, md, compressionType, tag, note) != STARCH_EXIT_SUCCESS) {
            fprintf(stderr, "ERROR: Could not write transformed/compressed data to output file pointer.\n");
            return STARCH_EXIT_FAILURE;
        }
    }

    /* 
        1. Read through inFp
        2. Transform a chromosome's worth of data ("record")
        3. Write record to outFp
        4. Add record description to metadata (md)
        5. Repeat 1-4 until EOF of BED input
        6. Calculate JSON string from metadata (md)
        7. Write JSON to outFp
        8. Take SHA-1 hash of JSON string
        9. Write 'offset' and 'hash' values to archive header section of outFp
       10. Close outFp
    */

#ifdef DEBUG
    fprintf(stderr, "\ttag: %s\n\tnote: %s\n", tag, note);
    STARCH2_printStarchHeader(*header);
#endif

    return STARCH_EXIT_SUCCESS;
}

int
STARCH2_transformHeaderedBEDInput(const FILE *inFp, Metadata **md, const CompressionType compressionType, const char *tag, const char *note)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_transformHeaderedBEDInput() ---\n");
#endif
    int c;
    int cIdx = 0;
    char untransformedBuffer[STARCH_BUFFER_MAX_LENGTH];
    char intermediateBuffer[STARCH_BUFFER_MAX_LENGTH];
    char transformedBuffer[STARCH_BUFFER_MAX_LENGTH];
    unsigned long lineIdx = 0UL;
    int64_t start = 0LL;
    int64_t stop = 0LL;
    int64_t previousStop = 0LL;
    int64_t lastPosition = 0LL;
    int64_t lcDiff = 0LL;
    int64_t coordDiff = 0LL;
    char *prevChromosome = NULL;
    char *chromosome = NULL;
    char *remainder = NULL;
    Boolean withinChr = kStarchFalse;
    unsigned long totalNonUniqueBases = 0UL;
    unsigned long totalUniqueBases = 0UL;
    size_t intermediateBufferLength = 0U;
    size_t currentTransformedBufferLength = 0U;
    size_t recIdx = 0U;
    size_t currentRecSize = 0U;
    size_t cumulativeRecSize = 0U;
    char *compressedFn = NULL;
    Metadata *firstRecord = NULL;
    char *json = NULL;
    CompressionType type = compressionType;
    unsigned char sha1Digest[STARCH2_MD_FOOTER_SHA1_LENGTH];
    char *base64EncodedSha1Digest = NULL;
    int zError = -1;
    char zBuffer[STARCH_Z_BUFFER_MAX_LENGTH] = {0};
    z_stream zStream;
    size_t zHave;
    int bzError = BZ_OK;
    unsigned int bzBytesConsumed = 0U;
    unsigned int bzBytesWritten = 0U;
    FILE *outFp = stdout;
    BZFILE *bzFp = NULL;
    char footerCumulativeRecordSizeBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + 1] = {0};
    char footerRemainderBuffer[STARCH2_MD_FOOTER_REMAINDER_LENGTH] = {0};
    char footerBuffer[STARCH2_MD_FOOTER_LENGTH] = {0};
    BedLineType lineType = kBedLineTypeUndefined;
    char nonCoordLineBuf[STARCH_BUFFER_MAX_LENGTH] = {0};
    Boolean nonCoordLineBufNeedsPrinting = kStarchFalse;
    char const *nullChr = "null";
    char const *nullCompressedFn = "null";
    
    /* increment total file size by header bytes */
#ifdef DEBUG
    fprintf(stderr, "\tincrementing file size by sizeof(header)\n");
#endif
    cumulativeRecSize += STARCH2_MD_HEADER_BYTE_LENGTH;

    compressedFn = (char *) malloc(STARCH_STREAM_METADATA_FILENAME_MAX_LENGTH);
    if (!compressedFn) {
        fprintf(stderr, "ERROR: Could not allocate space to compressed filename stub\n");
        return STARCH_EXIT_FAILURE;
    }

    /* set up compression streams */
    if (compressionType == kBzip2) {
#ifdef DEBUG
        fprintf(stderr, "\tsetting up bzip2 stream...\n");
#endif
        bzFp = BZ2_bzWriteOpen(&bzError, outFp, STARCH_BZ_COMPRESSION_LEVEL, STARCH_BZ_VERBOSITY, STARCH_BZ_WORKFACTOR);
        if (!bzFp) {
            fprintf(stderr, "ERROR: Could not instantiate BZFILE pointer\n");
            return STARCH_EXIT_FAILURE;
        }
        else if (bzError != BZ_OK) {
            switch (bzError) {
                case BZ_CONFIG_ERROR: {
                    fprintf(stderr, "ERROR: Bzip2 library has been miscompiled\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_PARAM_ERROR: {
                    fprintf(stderr, "ERROR: Stream is null, or block size, verbosity and work factor parameters are invalid\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_IO_ERROR: {
                    fprintf(stderr, "ERROR: The value of ferror(outFp) is nonzero -- check outFp\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_MEM_ERROR: {
                    fprintf(stderr, "ERROR: Not enough memory is available\n");
                    return STARCH_EXIT_FAILURE;
                }
                default: {
                    fprintf(stderr, "ERROR: Unknown error with BZ2_bzWriteOpen() (err: %d)\n", bzError);
                    return STARCH_EXIT_FAILURE;
                }
            }
        }
    }
    else if (compressionType == kGzip) {
#ifdef DEBUG
        fprintf(stderr, "\tsetting up gzip stream...\n");
#endif        
        zStream.zalloc = Z_NULL;
        zStream.zfree  = Z_NULL;
        zStream.opaque = Z_NULL;
        /* cf. http://www.zlib.net/manual.html for level information */
        /* zError = deflateInit2(&zStream, STARCH_Z_COMPRESSION_LEVEL, Z_DEFLATED, STARCH_Z_WINDOW_BITS, STARCH_Z_MEMORY_LEVEL, Z_DEFAULT_STRATEGY); */
        zError = deflateInit(&zStream, STARCH_Z_COMPRESSION_LEVEL);
        switch(zError) {
            case Z_MEM_ERROR: {
                fprintf(stderr, "ERROR: Not enough memory is available\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_STREAM_ERROR: {
                fprintf(stderr, "ERROR: Gzip initialization parameter is invalid (e.g., invalid method)\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_VERSION_ERROR: {
                fprintf(stderr, "ERROR: the zlib library version is incompatible with the version assumed by the caller (ZLIB_VERSION)\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_OK:
            default:
                break;
        }
    }

    /* fill up a "transformation" buffer with data and then compress it */
    while ((c = fgetc((FILE *)inFp)) != EOF) {
        untransformedBuffer[cIdx] = (char) c;
        if (c == '\n') {
            lineIdx++;
            untransformedBuffer[cIdx] = '\0';

            if (STARCH_createTransformTokens(untransformedBuffer, '\t', &chromosome, &start, &stop, &remainder, &lineType) == 0) 
            {
                if ( (lineType == kBedLineCoordinates) && ((!prevChromosome) || (strcmp(chromosome, prevChromosome) != 0)) ) 
                {
                    if (prevChromosome) 
                    {
		        if (STARCH_chromosomeInMetadataRecords((const Metadata *)firstRecord, chromosome) == STARCH_EXIT_SUCCESS) {
	    	            fprintf(stderr, "ERROR: Found same chromosome in earlier portion of file. Possible interleaving issue? Be sure to first sort input with sort-bed or remove --do-not-sort option from conversion script.\n");
                            return STARCH_FATAL_ERROR;
                        }
                        sprintf(compressedFn, "%s.%s", prevChromosome, tag);
#ifdef DEBUG                        
                        fprintf(stderr, "\t(final-between-chromosome) transformedBuffer:\n%s\n\t\tintermediateBuffer:\n%s\n", transformedBuffer, intermediateBuffer);
#endif
                        if (compressionType == kBzip2) {
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) finalizing current chromosome: %s\n", prevChromosome);
#endif
                            /* write transformed buffer to output stream */
                            BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
                            if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_PARAM_ERROR: {
                                        fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_SEQUENCE_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }

                            /* close bzip2 stream and collect/reset stats */
                            BZ2_bzWriteClose(&bzError, bzFp, STARCH_BZ_ABANDON, &bzBytesConsumed, &bzBytesWritten);
                            if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_SEQUENCE_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }
                            cumulativeRecSize += bzBytesWritten;
                            currentRecSize += bzBytesWritten;
                            bzBytesWritten = 0U;
                            bzFp = NULL;

                            if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx, totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                                fprintf(stderr, "ERROR: Could not update metadata %s\n", compressedFn);
                                return STARCH_FATAL_ERROR;
                            }

                            /* start again, anew */
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) resetting bzip2 stream...\n");
#endif
                            bzFp = BZ2_bzWriteOpen(&bzError, outFp, STARCH_BZ_COMPRESSION_LEVEL, STARCH_BZ_VERBOSITY, STARCH_BZ_WORKFACTOR);
                            if (!bzFp) {
                                fprintf(stderr, "ERROR: Could not instantiate BZFILE pointer\n");
                                return STARCH_EXIT_FAILURE;
                            }
                            else if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_CONFIG_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 library has been miscompiled\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_PARAM_ERROR: {
                                        fprintf(stderr, "ERROR: Stream is null, or block size, verbosity and work factor parameters are invalid\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: The value of ferror(outFp) is nonzero -- check outFp\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory is available\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWriteOpen() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }
                        }

                        else if (compressionType == kGzip) 
                        {
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) current chromosome: %s\n", prevChromosome);
                            fprintf(stderr, "\t(final-between-chromosome) transformedBuffer:\n%s\n", transformedBuffer);
#endif
                            zStream.next_in = (unsigned char *) transformedBuffer;
                            zStream.avail_in = (uInt) currentTransformedBufferLength;
                            do {
                                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                                zStream.next_out = (unsigned char *) zBuffer;
                                zError = deflate (&zStream, Z_FINISH);
                                switch (zError) {
                                    case Z_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                                        return STARCH_FATAL_ERROR;
                                    }                                    
                                    case Z_BUF_ERROR:
                                    default:
                                        break;
                                }
                                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                                cumulativeRecSize += zHave;
                                currentRecSize += zHave;
#ifdef DEBUG
                                fprintf(stderr, "\t(final-between-chromosome) writing: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                                fwrite(zBuffer, 1, zHave, stdout);
                                fflush(stdout);
                            } while (zStream.avail_out == 0);
                            assert(zStream.avail_in == 0);

#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) attempting to close z-stream...\n");
#endif
                            zError = deflateEnd(&zStream);
                            switch (zError) {
                                case Z_STREAM_ERROR: {
                                    fprintf(stderr, "ERROR: z-stream state is inconsistent\n");
                                    break;
                                }
                                case Z_DATA_ERROR: {
                                    fprintf(stderr, "ERROR: stream was freed prematurely\n");
                                    break;
                                }
                                case Z_OK:
                                default:
                                    break;
                            }
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) closed z-stream...\n");
                            fprintf(stderr, "\t(final-between-chromosome) updating metadata...\n");
#endif
                            if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx, totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                                fprintf(stderr, "ERROR: Could not update metadata %s\n", compressedFn);
                                return STARCH_FATAL_ERROR;
                            }

                            /* begin anew with a fresh compression z-stream */
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) creating fresh z-stream\n");
#endif
                            zStream.zalloc = Z_NULL;
                            zStream.zfree  = Z_NULL;
                            zStream.opaque = Z_NULL;
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) initializing z-stream\n");
#endif
                            /* zError = deflateInit2(&zStream, STARCH_Z_COMPRESSION_LEVEL, Z_DEFLATED, STARCH_Z_WINDOW_BITS, STARCH_Z_MEMORY_LEVEL, Z_DEFAULT_STRATEGY); */
                            zError = deflateInit(&zStream, STARCH_Z_COMPRESSION_LEVEL);
                            switch (zError) {
                                case Z_MEM_ERROR: {
                                    fprintf(stderr, "ERROR: Not enough memory is available\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_STREAM_ERROR: {
                                    fprintf(stderr, "ERROR: Gzip initialization parameter is invalid (e.g., invalid method)\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_VERSION_ERROR: {
                                    fprintf(stderr, "ERROR: the zlib library version is incompatible with the version assumed by the caller (ZLIB_VERSION)\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_OK:
                                default:
                                    break;
                            }
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) initialized z-stream\n");
#endif
                        }
                    }

                    /* create placeholder records at current chromosome */
                    sprintf(compressedFn, "%s.%s", chromosome, tag);
#ifdef DEBUG
                    fprintf(stderr, "\t(final-between-chromosome) creating placeholder md record at chromosome: %s (compressedFn: %s)\n", chromosome, compressedFn);
#endif
                    if (recIdx == 0) {
                        *md = NULL;
                        *md = STARCH_createMetadata(chromosome, compressedFn, 0ULL, 0UL, 0UL, 0UL);
                        if (!*md) { 
                            fprintf(stderr, "ERROR: Not enough memory is available\n");
                            return STARCH_EXIT_FAILURE;
                        }
                        firstRecord = *md;
                    }
                    else {
                        *md = STARCH_addMetadata(*md, chromosome, compressedFn, 0ULL, 0UL, 0UL, 0UL);
                    }

                    /* make previous chromosome the current chromosome */
                    if (prevChromosome != NULL) {
                        free(prevChromosome);
                        prevChromosome = NULL;
                    }
                    prevChromosome = (char *) malloc(strlen(chromosome) + 1);
                    if (!prevChromosome) {
                        fprintf(stderr, "ERROR: Could not allocate space for previous chromosome marker.");
                        return STARCH_FATAL_ERROR;
                    }
                    strncpy(prevChromosome, (const char *) chromosome, strlen(chromosome) + 1);

                    /* reset flag, lastPosition and lcDiff, increment record index */
#ifdef DEBUG
                    fprintf(stderr, "\t(final-between-chromosome) resetting per-chromosome stream transformation parameters...\n");
#endif
                    withinChr = kStarchFalse;
                    lastPosition = 0LL;
                    previousStop = 0LL;
                    lcDiff = 0LL;
                    lineIdx = 0UL;
                    totalNonUniqueBases = 0UL;
                    totalUniqueBases = 0UL;
                    recIdx++;
                    currentRecSize = 0UL;
                    transformedBuffer[currentTransformedBufferLength] = '\0';
                    currentTransformedBufferLength = 0U;
                }
                else if (lineType == kBedLineCoordinates)
                    withinChr = kStarchTrue;

                if (lineType != kBedLineCoordinates) {
                    strncat(nonCoordLineBuf, (const char *)chromosome, strlen(chromosome) + 1);
                    nonCoordLineBuf[strlen(nonCoordLineBuf)] = '\n';
                    nonCoordLineBufNeedsPrinting = kStarchTrue;
                }
                else {
                    if (nonCoordLineBufNeedsPrinting == kStarchTrue) {
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "%s", nonCoordLineBuf);
                        memset(nonCoordLineBuf, 0, strlen(nonCoordLineBuf));
                        nonCoordLineBufNeedsPrinting = kStarchFalse;
                    }
                    if (stop > start)
                        coordDiff = stop - start;
                    else {
                        fprintf(stderr, "ERROR: BED data is corrupt at line %lu (stop: %" PRId64 ", start: %" PRId64 ")\n", lineIdx, stop, start);
                        return STARCH_FATAL_ERROR;
                    }
                    if (coordDiff != lcDiff) {
                        lcDiff = coordDiff;
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "p%" PRId64 "\n", coordDiff);
                    }
                    if (lastPosition != 0) {
                        if (remainder)
                            sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\t%s\n", (start - lastPosition), remainder);
                        else
                            sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\n", (start - lastPosition));
                    }
                    else {
                        if (remainder)
                            sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\t%s\n", start, remainder);
                        else 
                            sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\n", start);
                    }
                    intermediateBufferLength = strlen(intermediateBuffer);                

                    if ((currentTransformedBufferLength + intermediateBufferLength) < STARCH_BUFFER_MAX_LENGTH) {
                        /* append intermediateBuffer to transformedBuffer */
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) appending intermediateBuffer to transformedBuffer (old currentTransformedBufferLength: %lu)\n%s\n", currentTransformedBufferLength, intermediateBuffer);
#endif
                        memcpy(transformedBuffer + currentTransformedBufferLength, intermediateBuffer, intermediateBufferLength);
                        currentTransformedBufferLength += intermediateBufferLength;
                        transformedBuffer[currentTransformedBufferLength] = '\0';
                        memset(intermediateBuffer, 0, intermediateBufferLength + 1);
                    }
                    else {
                        /* compress transformedBuffer[] and send to stdout */
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) to be compressed -- transformedBuffer:\n%s\n", transformedBuffer);
#endif                    
                        if (compressionType == kBzip2) {
#ifdef DEBUG
                            fprintf(stderr, "\t(intermediate) current chromosome: %s\n", prevChromosome);
#endif
                            BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
                            if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_PARAM_ERROR: {
                                        fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_SEQUENCE_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }                        
                        }
                        else if (compressionType == kGzip) {
#ifdef DEBUG
                            fprintf(stderr, "\t(intermediate) current chromosome: %s\n", prevChromosome);
#endif
                            zStream.next_in = (unsigned char *) transformedBuffer;
                            zStream.avail_in = (uInt) currentTransformedBufferLength;
                            do {
                                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                                zStream.next_out = (unsigned char *) zBuffer;
                                zError = deflate (&zStream, Z_NO_FLUSH);
                                switch (zError) {
                                    case Z_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                                        return STARCH_FATAL_ERROR;
                                    }
                                    case Z_BUF_ERROR:
                                    default:
                                        break;
                                }
                                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                                cumulativeRecSize += zHave;
                                currentRecSize += zHave;
#ifdef DEBUG
                                fprintf(stderr, "\t(intermediate) written: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                                fwrite(zBuffer, 1, zHave, stdout);
                                fflush(stdout);
                            } while (zStream.avail_out == 0);

                            zStream.next_in = (unsigned char *) intermediateBuffer;
                            zStream.avail_in = (uInt) strlen(intermediateBuffer);
                            do {
                                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                                zStream.next_out = (unsigned char *) zBuffer;
                                zError = deflate (&zStream, Z_NO_FLUSH);
                                switch (zError) {
                                    case Z_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                                        return STARCH_FATAL_ERROR;
                                    }
                                    case Z_BUF_ERROR:
                                    default:
                                        break;
                                }
                                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                                cumulativeRecSize += zHave;
                                currentRecSize += zHave;
#ifdef DEBUG
                                fprintf(stderr, "\t(intermediate) written: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                                fwrite(zBuffer, 1, zHave, stdout);
                                fflush(stdout);
                            } while (zStream.avail_out == 0);
                        }

                        memcpy(transformedBuffer, intermediateBuffer, strlen(intermediateBuffer) + 1);
                        currentTransformedBufferLength = strlen(intermediateBuffer);
                        memset(intermediateBuffer, 0, strlen(intermediateBuffer) + 1);
                        intermediateBufferLength = 0;
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) end-of-loop: transformedBuffer:\n%s\n\t\tintermediateBuffer:\n%s\n", transformedBuffer, intermediateBuffer);
#endif
                    }

                    lastPosition = stop;
                    totalNonUniqueBases += (Bed::BaseCountType) (stop - start);
                    if (previousStop <= start)
                        totalUniqueBases += (Bed::BaseCountType) (stop - start);
                    else if (previousStop < stop)
                        totalUniqueBases += (Bed::BaseCountType) (stop - previousStop);
                    previousStop = (stop > previousStop) ? stop : previousStop;
                }

                if (withinChr == kStarchTrue) 
                    free(chromosome), chromosome = NULL;
                if (remainder) 
                    free(remainder), remainder = NULL;
                cIdx = 0;
            }
            else {
                fprintf(stderr, "ERROR: BED data could not be transformed.\n");
                return STARCH_FATAL_ERROR;
            }
        }
        else
            cIdx++;
     }
    
    lineIdx++;
    sprintf(compressedFn, "%s.%s", prevChromosome, tag);

#ifdef DEBUG
    fprintf(stderr, "\t(last-pass) transformedBuffer:\n%s\n\t\tintermediateBuffer:\n%s\n", transformedBuffer, intermediateBuffer);
    /*fprintf(stderr, "\t(last-pass) to be compressed - transformedBuffer:\n%s\n", transformedBuffer);*/
#endif
    /* last-pass, bzip2 */
    if (compressionType == kBzip2) {
#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) current chromosome: %s\n", prevChromosome);
#endif
        if (currentTransformedBufferLength > 0) 
        {
            BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
            if (bzError != BZ_OK) {
                switch (bzError) {
                    case BZ_PARAM_ERROR: {
                        fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    case BZ_SEQUENCE_ERROR: {
                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    case BZ_IO_ERROR: {
                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    default: {
                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                        return STARCH_EXIT_FAILURE;
                    }
                }
            }
        }
#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) attempting to close bzip2-stream...\n");
#endif
        BZ2_bzWriteClose(&bzError, bzFp, STARCH_BZ_ABANDON, &bzBytesConsumed, &bzBytesWritten);
        if (bzError != BZ_OK) {
            switch (bzError) {
                case BZ_PARAM_ERROR: {
                    fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_SEQUENCE_ERROR: {
                    fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_IO_ERROR: {
                    fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                    return STARCH_EXIT_FAILURE;
                }
                default: {
                    fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                    return STARCH_EXIT_FAILURE;
                }
            }
        }
        cumulativeRecSize += bzBytesWritten;
        currentRecSize += bzBytesWritten;

#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) closed bzip2-stream...\n");
#endif
    }

    /* last-pass, gzip */
    else if (compressionType == kGzip) {
#ifdef DEBUG
        /*fprintf(stderr, "\t(last-pass) to be compressed - transformedBuffer:\n%s\n", transformedBuffer);*/
#endif        
        if (currentTransformedBufferLength > 0) 
        {
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) current chromosome: %s\n", prevChromosome);
#endif
            zStream.next_in = (unsigned char *) transformedBuffer;
            zStream.avail_in = (uInt) currentTransformedBufferLength;
            do {
                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                zStream.next_out = (unsigned char *) zBuffer;
                zError = deflate(&zStream, Z_FINISH);
                switch (zError) {
                    case Z_MEM_ERROR: {
                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                        return STARCH_FATAL_ERROR;
                    }
                    case Z_BUF_ERROR:
                    default:
                        break;
                }
                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                cumulativeRecSize += zHave;
                currentRecSize += zHave;
#ifdef DEBUG
                fprintf(stderr, "\t(last-pass) written: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                fwrite(zBuffer, 1, zHave, stdout);
                fflush(stdout);
            } while (zStream.avail_out == 0);
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) attempting to close z-stream...\n");
#endif
            deflateEnd(&zStream);
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) closed z-stream...\n");
#endif
        }
    }

#ifdef DEBUG
    fprintf(stderr, "\t(last-pass) updating last md record...\n");
#endif
    if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx,  totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
        /* 
           If the stream or input file contains no BED records, then the Metadata pointer md will
           be NULL, as will the char pointer prevChromosome. So we put in a stub metadata record.
        */
        lineIdx = 0ULL;
        *md = NULL;
        *md = STARCH_createMetadata(nullChr, nullCompressedFn, currentRecSize, lineIdx, 0UL, 0UL);
        if (!*md) {
            fprintf(stderr, "ERROR: Not enough memory is available\n");
            return STARCH_EXIT_FAILURE;
        }
        firstRecord = *md;
    }        

    /* reset metadata pointer */
    *md = firstRecord;

    /* write metadata */
#ifdef DEBUG
    fprintf(stderr, "\twriting md to output stream (as JSON)...\n");
#endif
    /* this is the custom header version of the parser, so we set headerFlag to TRUE */
    STARCH_writeJSONMetadata(*md, &json, &type, kStarchTrue, note);
    fwrite(json, 1, strlen(json), stdout);
    fflush(stdout);

    /* write metadata signature */
#ifdef DEBUG
    fprintf(stderr, "\twriting md signature...\n");
#endif
    STARCH_SHA1_All((const unsigned char *)json, strlen(json), sha1Digest);

    /* encode signature in base64 encoding */
#ifdef DEBUG
    fprintf(stderr, "\tencoding md signature...\n");
#endif
    STARCH_encodeBase64(&base64EncodedSha1Digest, (const size_t) STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH, (const unsigned char *) sha1Digest, (const size_t) STARCH2_MD_FOOTER_SHA1_LENGTH);

    /* build footer */
#ifdef DEBUG
    fprintf(stderr, "\tWARNING:\nmdLength: %llu\nmd   - [%s]\nsha1 - [%s]\n", (unsigned long long) strlen(json), json, sha1Digest);
    fprintf(stderr, "\twriting offset and signature to output stream...\n");
#endif
    sprintf(footerCumulativeRecordSizeBuffer, "%020llu", (unsigned long long) cumulativeRecSize); /* we cast this size_t to an unsigned long long in order to allow warning-free compilation with an ISO C++ compiler like g++ */
#ifdef DEBUG
    fprintf(stderr, "\tfooterCumulativeRecordSizeBuffer: %s\n", footerCumulativeRecordSizeBuffer);
#endif
    memcpy(footerBuffer, footerCumulativeRecordSizeBuffer, strlen(footerCumulativeRecordSizeBuffer));
    memcpy(footerBuffer + STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH, base64EncodedSha1Digest, STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1); /* strip trailing null */
    memset(footerRemainderBuffer, STARCH2_MD_FOOTER_REMAINDER_UNUSED_CHAR, (size_t) STARCH2_MD_FOOTER_REMAINDER_LENGTH);
#ifdef DEBUG
    fprintf(stderr, "\tfooterRemainderBuffer: [%s]\n", footerRemainderBuffer);
#endif
    memcpy(footerBuffer + STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1, footerRemainderBuffer, STARCH2_MD_FOOTER_REMAINDER_LENGTH); /* don't forget to offset pointer index by -1 for base64-sha1's null */
    footerBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1 + STARCH2_MD_FOOTER_REMAINDER_LENGTH - 1] = '\0';
    footerBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1 + STARCH2_MD_FOOTER_REMAINDER_LENGTH - 2] = '\n';
    fprintf(stdout, "%s", footerBuffer);
    fflush(stdout);

    if (json)
        free(json), json = NULL;
    if (compressedFn)
        free(compressedFn), compressedFn = NULL;
    if (prevChromosome)
        free(prevChromosome), prevChromosome = NULL;
    if (base64EncodedSha1Digest)
        free(base64EncodedSha1Digest), base64EncodedSha1Digest = NULL;

    return STARCH_EXIT_SUCCESS;
}

int
STARCH2_transformHeaderlessBEDInput(const FILE *inFp, Metadata **md, const CompressionType compressionType, const char *tag, const char *note)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_transformHeaderlessBEDInput() ---\n");
#endif
    int c;
    int cIdx = 0;
    char untransformedBuffer[STARCH_BUFFER_MAX_LENGTH + 1] = {0};
    char intermediateBuffer[STARCH_BUFFER_MAX_LENGTH + 1] = {0};
    char transformedBuffer[STARCH_BUFFER_MAX_LENGTH + 1] = {0};
    unsigned long lineIdx = 0UL;
    int64_t start = 0LL;
    int64_t stop = 0LL;
    int64_t previousStop = 0LL;
    int64_t lastPosition = 0LL;
    int64_t lcDiff = 0LL;
    int64_t coordDiff = 0LL;
    char *prevChromosome = NULL;
    char *chromosome = NULL;
    char *remainder = NULL;
    Boolean withinChr = kStarchFalse;
    unsigned long totalNonUniqueBases = 0UL;
    unsigned long totalUniqueBases = 0UL;
    size_t intermediateBufferLength = 0U;
    size_t currentTransformedBufferLength = 0U;
    size_t recIdx = 0U;
    size_t currentRecSize = 0U;
    size_t cumulativeRecSize = 0U;
    char *compressedFn = NULL;
    Metadata *firstRecord = NULL;
    char *json = NULL;
    char *jsonCopy = NULL;
    CompressionType type = compressionType;
    unsigned char sha1Digest[STARCH2_MD_FOOTER_SHA1_LENGTH];
    char *base64EncodedSha1Digest = NULL;
    int zError = -1;
    char zBuffer[STARCH_Z_BUFFER_MAX_LENGTH] = {0};
    z_stream zStream;
    size_t zHave;
    int bzError = BZ_OK;
    unsigned int bzBytesConsumed = 0U;
    unsigned int bzBytesWritten = 0U;
    FILE *outFp = stdout;
    BZFILE *bzFp = NULL;
    char footerCumulativeRecordSizeBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + 1] = {0};
    char footerRemainderBuffer[STARCH2_MD_FOOTER_REMAINDER_LENGTH] = {0};
    char footerBuffer[STARCH2_MD_FOOTER_LENGTH] = {0};
    char const *nullChr = "null";
    char const *nullCompressedFn = "null";

    /* increment total file size by header bytes */
#ifdef DEBUG
    fprintf(stderr, "\tincrementing file size by sizeof(header)\n");
#endif
    cumulativeRecSize += STARCH2_MD_HEADER_BYTE_LENGTH;

    compressedFn = (char *) malloc(STARCH_STREAM_METADATA_FILENAME_MAX_LENGTH);
    if (!compressedFn) {
        fprintf(stderr, "ERROR: Could not allocate space to compressed filename stub\n");
        return STARCH_EXIT_FAILURE;
    }

    /* set up compression streams */
    if (compressionType == kBzip2) {
#ifdef DEBUG
        fprintf(stderr, "\tsetting up bzip2 stream...\n");
#endif
        bzFp = BZ2_bzWriteOpen(&bzError, outFp, STARCH_BZ_COMPRESSION_LEVEL, STARCH_BZ_VERBOSITY, STARCH_BZ_WORKFACTOR);
        if (!bzFp) {
            fprintf(stderr, "ERROR: Could not instantiate BZFILE pointer\n");
            return STARCH_EXIT_FAILURE;
        }
        else if (bzError != BZ_OK) {
            switch (bzError) {
                case BZ_CONFIG_ERROR: {
                    fprintf(stderr, "ERROR: Bzip2 library has been miscompiled\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_PARAM_ERROR: {
                    fprintf(stderr, "ERROR: Stream is null, or block size, verbosity and work factor parameters are invalid\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_IO_ERROR: {
                    fprintf(stderr, "ERROR: The value of ferror(outFp) is nonzero -- check outFp\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_MEM_ERROR: {
                    fprintf(stderr, "ERROR: Not enough memory is available\n");
                    return STARCH_EXIT_FAILURE;
                }
                default: {
                    fprintf(stderr, "ERROR: Unknown error with BZ2_bzWriteOpen() (err: %d)\n", bzError);
                    return STARCH_EXIT_FAILURE;
                }
            }
        }
    }
    else if (compressionType == kGzip) {
#ifdef DEBUG
        fprintf(stderr, "\tsetting up gzip stream...\n");
#endif        
        zStream.zalloc = Z_NULL;
        zStream.zfree  = Z_NULL;
        zStream.opaque = Z_NULL;
        /* cf. http://www.zlib.net/manual.html for level information */
        /* zError = deflateInit2(&zStream, STARCH_Z_COMPRESSION_LEVEL, Z_DEFLATED, STARCH_Z_WINDOW_BITS, STARCH_Z_MEMORY_LEVEL, Z_DEFAULT_STRATEGY); */
        zError = deflateInit(&zStream, STARCH_Z_COMPRESSION_LEVEL);
        switch(zError) {
            case Z_MEM_ERROR: {
                fprintf(stderr, "ERROR: Not enough memory is available\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_STREAM_ERROR: {
                fprintf(stderr, "ERROR: Gzip initialization parameter is invalid (e.g., invalid method)\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_VERSION_ERROR: {
                fprintf(stderr, "ERROR: the zlib library version is incompatible with the version assumed by the caller (ZLIB_VERSION)\n");
                return STARCH_EXIT_FAILURE;
            }
            case Z_OK:
            default:
                break;
        }
    }

    /* fill up a "transformation" buffer with data and then compress it */
    while ((c = fgetc((FILE *)inFp)) != EOF) {
        untransformedBuffer[cIdx] = (char) c;
        if (c == '\n') {
            lineIdx++;
            untransformedBuffer[cIdx] = '\0';

            if (STARCH_createTransformTokensForHeaderlessInput(untransformedBuffer, '\t', &chromosome, &start, &stop, &remainder) == 0) 
            {
                if ( (!prevChromosome) || (strcmp(chromosome, prevChromosome) != 0) ) {
                    if (prevChromosome) {                        
		        if (STARCH_chromosomeInMetadataRecords((const Metadata *)firstRecord, chromosome) == STARCH_EXIT_SUCCESS) {
	    	            fprintf(stderr, "ERROR: Found same chromosome in earlier portion of file. Possible interleaving issue? Be sure to first sort input with sort-bed or remove --do-not-sort option from conversion script.\n");
                            return STARCH_FATAL_ERROR;
                        }
                        sprintf(compressedFn, "%s.%s", prevChromosome, tag);
#ifdef DEBUG                        
                        fprintf(stderr, "\t(final-between-chromosome) transformedBuffer:\n%s\n", transformedBuffer);
#endif
                        if (compressionType == kBzip2) {
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) finalizing current chromosome: %s\n", prevChromosome);
#endif
                            /* write transformed buffer to output stream */
                            BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
                            if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_PARAM_ERROR: {
                                        fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_SEQUENCE_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }

                            /* close bzip2 stream and collect/reset stats */
                            BZ2_bzWriteClose(&bzError, bzFp, STARCH_BZ_ABANDON, &bzBytesConsumed, &bzBytesWritten);
                            if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_SEQUENCE_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }
                            cumulativeRecSize += bzBytesWritten;
                            currentRecSize += bzBytesWritten;
                            bzBytesWritten = 0U;
                            bzFp = NULL;

                            if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx, totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                                fprintf(stderr, "ERROR: Could not update metadata %s\n", compressedFn);
                                return STARCH_FATAL_ERROR;
                            }

                            /* start again, anew, with a fresh bzip2 BZFILE pointer */
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) resetting bzip2 stream...\n");
#endif
                            bzFp = BZ2_bzWriteOpen(&bzError, outFp, STARCH_BZ_COMPRESSION_LEVEL, STARCH_BZ_VERBOSITY, STARCH_BZ_WORKFACTOR);
                            if (!bzFp) {
                                fprintf(stderr, "ERROR: Could not instantiate BZFILE pointer\n");
                                return STARCH_EXIT_FAILURE;
                            }
                            else if (bzError != BZ_OK) {
                                switch (bzError) {
                                    case BZ_CONFIG_ERROR: {
                                        fprintf(stderr, "ERROR: Bzip2 library has been miscompiled\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_PARAM_ERROR: {
                                        fprintf(stderr, "ERROR: Stream is null, or block size, verbosity and work factor parameters are invalid\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_IO_ERROR: {
                                        fprintf(stderr, "ERROR: The value of ferror(outFp) is nonzero -- check outFp\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    case BZ_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory is available\n");
                                        return STARCH_EXIT_FAILURE;
                                    }
                                    default: {
                                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWriteOpen() (err: %d)\n", bzError);
                                        return STARCH_EXIT_FAILURE;
                                    }
                                }
                            }
                        }

                        else if (compressionType == kGzip) 
                        {
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) current chromosome: %s\n", prevChromosome);
                            fprintf(stderr, "\t(final-between-chromosome) transformedBuffer:\n%s\n", transformedBuffer);
#endif
                            zStream.next_in = (unsigned char *) transformedBuffer;
                            zStream.avail_in = (uInt) currentTransformedBufferLength;
                            do {
                                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                                zStream.next_out = (unsigned char *) zBuffer;
                                zError = deflate (&zStream, Z_FINISH);
                                switch (zError) {
                                    case Z_MEM_ERROR: {
                                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                                        return STARCH_FATAL_ERROR;
                                    }                                    
                                    case Z_BUF_ERROR:
                                    default:
                                        break;
                                }
                                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                                cumulativeRecSize += zHave;
                                currentRecSize += zHave;
#ifdef DEBUG
                                fprintf(stderr, "\t(final-between-chromosome) writing: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                                fwrite(zBuffer, 1, zHave, stdout);
                                fflush(stdout);
                            } while (zStream.avail_out == 0);
                            assert(zStream.avail_in == 0);

#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) attempting to close z-stream...\n");
#endif
                            zError = deflateEnd(&zStream);
                            switch (zError) {
                                case Z_STREAM_ERROR: {
                                    fprintf(stderr, "ERROR: z-stream state is inconsistent\n");
                                    break;
                                }
                                case Z_DATA_ERROR: {
                                    fprintf(stderr, "ERROR: stream was freed prematurely\n");
                                    break;
                                }
                                case Z_OK:
                                default:
                                    break;
                            }
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) closed z-stream...\n");
                            fprintf(stderr, "\t(final-between-chromosome) updating metadata...\n");
#endif
                            if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx, totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
                                fprintf(stderr, "ERROR: Could not update metadata %s\n", compressedFn);
                                return STARCH_FATAL_ERROR;
                            }

                            /* begin anew with a fresh compression z-stream */
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) creating fresh z-stream\n");
#endif
                            zStream.zalloc = Z_NULL;
                            zStream.zfree  = Z_NULL;
                            zStream.opaque = Z_NULL;
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) initializing z-stream\n");
#endif
                            /* zError = deflateInit2(&zStream, STARCH_Z_COMPRESSION_LEVEL, Z_DEFLATED, STARCH_Z_WINDOW_BITS, STARCH_Z_MEMORY_LEVEL, Z_DEFAULT_STRATEGY); */
                            zError = deflateInit(&zStream, STARCH_Z_COMPRESSION_LEVEL);
                            switch (zError) {
                                case Z_MEM_ERROR: {
                                    fprintf(stderr, "ERROR: Not enough memory is available\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_STREAM_ERROR: {
                                    fprintf(stderr, "ERROR: Gzip initialization parameter is invalid (e.g., invalid method)\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_VERSION_ERROR: {
                                    fprintf(stderr, "ERROR: the zlib library version is incompatible with the version assumed by the caller (ZLIB_VERSION)\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case Z_OK:
                                default:
                                    break;
                            }
#ifdef DEBUG
                            fprintf(stderr, "\t(final-between-chromosome) initialized z-stream\n");
#endif
                        }
                    }

                    /* create placeholder records at current chromosome */
                    sprintf(compressedFn, "%s.%s", chromosome, tag);
#ifdef DEBUG
                    fprintf(stderr, "\t(final-between-chromosome) creating placeholder md record at chromosome: %s (compressedFn: %s)\n", chromosome, compressedFn);
#endif
                    if (recIdx == 0) {
                        *md = NULL;
                        *md = STARCH_createMetadata(chromosome, compressedFn, 0ULL, 0UL, 0UL, 0UL);
                        if (!*md) { 
                            fprintf(stderr, "ERROR: Not enough memory is available\n");
                            return STARCH_EXIT_FAILURE;
                        }
                        firstRecord = *md;
                    }
                    else {
                        *md = STARCH_addMetadata(*md, chromosome, compressedFn, 0ULL, 0UL, 0UL, 0UL);
                    }

                    /* make previous chromosome the current chromosome */
                    if (prevChromosome != NULL) {
                        free(prevChromosome);
                        prevChromosome = NULL;
                    }
                    prevChromosome = (char *) malloc(strlen(chromosome) + 1);
                    if (!prevChromosome) {
                        fprintf(stderr, "ERROR: Could not allocate space for previous chromosome marker.");
                        return STARCH_FATAL_ERROR;
                    }
                    strncpy(prevChromosome, (const char *) chromosome, strlen(chromosome) + 1);

                    /* reset flag, lastPosition and lcDiff, increment record index */
#ifdef DEBUG
                    fprintf(stderr, "\t(final-between-chromosome) resetting per-chromosome stream transformation parameters...\n");
#endif
                    withinChr = kStarchFalse;
                    lastPosition = 0LL;
                    previousStop = 0LL;
                    lcDiff = 0LL;
                    lineIdx = 0UL;
                    totalNonUniqueBases = 0UL;
                    totalUniqueBases = 0UL;
                    recIdx++;
                    currentRecSize = 0UL;
                    transformedBuffer[currentTransformedBufferLength] = '\0';
                    currentTransformedBufferLength = 0U;
                }
                else 
                    withinChr = kStarchTrue;

                /* transform */
                if (stop > start)
                    coordDiff = stop - start;
                else {
                    fprintf(stderr, "ERROR: BED data is corrupt at line %lu (stop: %" PRId64 ", start: %" PRId64 ")\n", lineIdx, stop, start);
                    return STARCH_FATAL_ERROR;
                }
                if (coordDiff != lcDiff) {
                    lcDiff = coordDiff;
#ifdef DEBUG
                    fprintf(stderr, "\t(intermediate) A -- \np%" PRId64 "\n", coordDiff);
#endif
                    sprintf(intermediateBuffer + strlen(intermediateBuffer), "p%" PRId64 "\n", coordDiff);
                }
                if (lastPosition != 0) {
                    if (remainder) {                        
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) B --\n%" PRId64 "\t%s\n", (start - lastPosition), remainder);
#endif
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\t%s\n", (start - lastPosition), remainder);
                    }
                    else {
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) C --\n%" PRId64 "\n", (start - lastPosition));
#endif
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\n", (start - lastPosition));
                    }
                }
                else {
                    if (remainder) {
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) D --\n%" PRId64 "\t%s\n", start, remainder);
#endif
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\t%s\n", start, remainder);
                    }
                    else {
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) E --\n%" PRId64 "\n", start);
#endif
                        sprintf(intermediateBuffer + strlen(intermediateBuffer), "%" PRId64 "\n", start);
                    }
                }
                intermediateBufferLength = strlen(intermediateBuffer);
                
#ifdef DEBUG
                fprintf(stderr, "\t(intermediate) state of intermediateBuffer before test:\n%s\n", intermediateBuffer);
#endif                

                if ((currentTransformedBufferLength + intermediateBufferLength) < STARCH_BUFFER_MAX_LENGTH) {
                    /* append intermediateBuffer to transformedBuffer */
#ifdef DEBUG
                    fprintf(stderr, "\t(intermediate) appending intermediateBuffer to transformedBuffer\n");
#endif
                    memcpy(transformedBuffer + currentTransformedBufferLength, intermediateBuffer, intermediateBufferLength);
                    currentTransformedBufferLength += intermediateBufferLength;
                    transformedBuffer[currentTransformedBufferLength] = '\0';
                    memset(intermediateBuffer, 0, intermediateBufferLength + 1);
                }
                else {
                    /* compress transformedBuffer[] and send to stdout */
#ifdef DEBUG
                    fprintf(stderr, "\t(intermediate) to be compressed -- transformedBuffer:\n%s\n", transformedBuffer);
#endif                    
                    if (compressionType == kBzip2) {
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) current chromosome: %s\n", prevChromosome);
#endif
                        BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
                        if (bzError != BZ_OK) {
                            switch (bzError) {
                                case BZ_PARAM_ERROR: {
                                    fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case BZ_SEQUENCE_ERROR: {
                                    fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                case BZ_IO_ERROR: {
                                    fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                                    return STARCH_EXIT_FAILURE;
                                }
                                default: {
                                    fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                                    return STARCH_EXIT_FAILURE;
                                }
                            }
                        }                        
                    }
                    else if (compressionType == kGzip) {
#ifdef DEBUG
                        fprintf(stderr, "\t(intermediate) current chromosome: %s\n", prevChromosome);
#endif
                        zStream.next_in = (unsigned char *) transformedBuffer;
                        zStream.avail_in = (uInt) currentTransformedBufferLength;
                        do {
                            zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                            zStream.next_out = (unsigned char *) zBuffer;
                            zError = deflate (&zStream, Z_NO_FLUSH);
                            switch (zError) {
                                case Z_MEM_ERROR: {
                                    fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                                    return STARCH_FATAL_ERROR;
                                }
                                case Z_BUF_ERROR:
                                default:
                                    break;
                            }
                            zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                            cumulativeRecSize += zHave;
                            currentRecSize += zHave;
#ifdef DEBUG
                            fprintf(stderr, "\t(intermediate) written: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                            fwrite(zBuffer, 1, zHave, stdout);
                            fflush(stdout);
                        } while (zStream.avail_out == 0);
                    }                    

                    memcpy(transformedBuffer, intermediateBuffer, strlen(intermediateBuffer) + 1);
                    currentTransformedBufferLength = strlen(intermediateBuffer);
                    memset(intermediateBuffer, 0, strlen(intermediateBuffer) + 1);
                    intermediateBufferLength = 0;
#ifdef DEBUG
                    fprintf(stderr, "\t(intermediate) end-of-loop: transformedBuffer:\n%s\n\t\tintermediateBuffer:\n%s\n", transformedBuffer, intermediateBuffer);
#endif
                }

                lastPosition = stop;
                totalNonUniqueBases += (Bed::BaseCountType) (stop - start);
                if (previousStop <= start)
                    totalUniqueBases += (Bed::BaseCountType) (stop - start);
                else if (previousStop < stop)
                    totalUniqueBases += (Bed::BaseCountType) (stop - previousStop);
                previousStop = (stop > previousStop) ? stop : previousStop;                

                if (withinChr == kStarchTrue) 
                    free(chromosome), chromosome = NULL;
                if (remainder) 
                    free(remainder), remainder = NULL;
                cIdx = 0;
            }
            else {
                fprintf(stderr, "ERROR: BED data could not be transformed.\n");
                return STARCH_FATAL_ERROR;
            }
        }
        else
            cIdx++;
    }
    
    lineIdx++;
    sprintf(compressedFn, "%s.%s", prevChromosome, tag);

#ifdef DEBUG
    fprintf(stderr, "\t(last-pass) transformedBuffer:\n%s\n\t\tintermediateBuffer:\n%s\n", transformedBuffer, intermediateBuffer);
#endif
    /* last-pass, bzip2 */
    if (compressionType == kBzip2) {
#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) current chromosome: %s\n", prevChromosome);
#endif
        if (currentTransformedBufferLength > 0) 
        {
            BZ2_bzWrite(&bzError, bzFp, transformedBuffer, (int) currentTransformedBufferLength);
            if (bzError != BZ_OK) {
                switch (bzError) {
                    case BZ_PARAM_ERROR: {
                        fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    case BZ_SEQUENCE_ERROR: {
                        fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    case BZ_IO_ERROR: {
                        fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                        return STARCH_EXIT_FAILURE;
                    }
                    default: {
                        fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                        return STARCH_EXIT_FAILURE;
                    }
                }
            }
        }

#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) attempting to close bzip2 stream...\n");
#endif
        BZ2_bzWriteClose(&bzError, bzFp, STARCH_BZ_ABANDON, &bzBytesConsumed, &bzBytesWritten);
        if (bzError != BZ_OK) {
            switch (bzError) {
                case BZ_PARAM_ERROR: {
                    fprintf(stderr, "ERROR: Stream is NULL, transformedBuffer is NULL, or currentTransformedBufferLength is negative\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_SEQUENCE_ERROR: {
                    fprintf(stderr, "ERROR: Bzip2 streams are out of sequence\n");
                    return STARCH_EXIT_FAILURE;
                }
                case BZ_IO_ERROR: {
                    fprintf(stderr, "ERROR: There is an error writing the compressed data to the bz stream\n");
                    return STARCH_EXIT_FAILURE;
                }
                default: {
                    fprintf(stderr, "ERROR: Unknown error with BZ2_bzWrite() (err: %d)\n", bzError);
                    return STARCH_EXIT_FAILURE;
                }
            }
        }
        cumulativeRecSize += bzBytesWritten;
        currentRecSize += bzBytesWritten;

#ifdef DEBUG
        fprintf(stderr, "\t(last-pass) closed bzip2 stream...\n");
#endif
    }

    /* last-pass, gzip */
    else if (compressionType == kGzip) {
#ifdef DEBUG
        /*fprintf(stderr, "\t(last-pass) to be compressed - transformedBuffer:\n%s\n", transformedBuffer);*/
#endif        
        if (currentTransformedBufferLength > 0) 
        {
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) current chromosome: %s\n", prevChromosome);
#endif
            zStream.next_in = (unsigned char *) transformedBuffer;
            zStream.avail_in = (uInt) currentTransformedBufferLength;
            do {
                zStream.avail_out = STARCH_Z_BUFFER_MAX_LENGTH;
                zStream.next_out = (unsigned char *) zBuffer;
                zError = deflate(&zStream, Z_FINISH);
                switch (zError) {
                    case Z_MEM_ERROR: {
                        fprintf(stderr, "ERROR: Not enough memory to compress data\n");
                        return STARCH_FATAL_ERROR;
                    }
                    case Z_BUF_ERROR:
                    default:
                        break;
                }
                zHave = STARCH_Z_BUFFER_MAX_LENGTH - zStream.avail_out;
                cumulativeRecSize += zHave;
                currentRecSize += zHave;
#ifdef DEBUG
                fprintf(stderr, "\t(last-pass) written: %zu bytes\tcurrent record size: %zu\n", cumulativeRecSize, currentRecSize);
#endif
                fwrite(zBuffer, 1, zHave, stdout);
                fflush(stdout);
            } while (zStream.avail_out == 0);
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) attempting to close z-stream...\n");
#endif
            deflateEnd(&zStream);
#ifdef DEBUG
            fprintf(stderr, "\t(last-pass) closed z-stream...\n");
#endif
        }
    }

#ifdef DEBUG
    fprintf(stderr, "\t(last-pass) updating last md record...\n");
#endif
    if (STARCH_updateMetadataForChromosome(md, prevChromosome, compressedFn, currentRecSize, lineIdx,  totalNonUniqueBases, totalUniqueBases) != STARCH_EXIT_SUCCESS) {
        /* 
           If the stream or input file contains no BED records, then the Metadata pointer md will
           be NULL, as will the char pointer prevChromosome. So we put in a stub metadata record.
        */
        lineIdx = 0ULL;
        *md = NULL;
        *md = STARCH_createMetadata(nullChr, nullCompressedFn, currentRecSize, lineIdx, 0UL, 0UL);
        if (!*md) { 
            fprintf(stderr, "ERROR: Not enough memory is available\n");
            return STARCH_EXIT_FAILURE;
        }
        firstRecord = *md;
    }        

    /* reset metadata pointer */
    *md = firstRecord;

    /* write metadata */
#ifdef DEBUG
    fprintf(stderr, "\twriting md to output stream (as JSON)...\n");
#endif
    STARCH_writeJSONMetadata(*md, &json, &type, 0, note);
    fwrite(json, 1, strlen(json), stdout);
    fflush(stdout);

    /* write metadata signature */
#ifdef DEBUG
    fprintf(stderr, "\twriting md signature...\n");
#endif
    jsonCopy = strdup(json);
    STARCH_SHA1_All((const unsigned char *)jsonCopy, strlen(jsonCopy), sha1Digest);
    free(jsonCopy);

    /* encode signature in base64 encoding */
#ifdef DEBUG
    fprintf(stderr, "\tencoding md signature...\n");
#endif
    STARCH_encodeBase64(&base64EncodedSha1Digest, (const size_t) STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH, (const unsigned char *) sha1Digest, (const size_t) STARCH2_MD_FOOTER_SHA1_LENGTH);

    /* build footer */
#ifdef DEBUG
    fprintf(stderr, "\tWARNING:\nmdLength: %llu\nmd   - [%s]\nsha1 - [%s]\n", (unsigned long long) strlen(json), json, sha1Digest);
    fprintf(stderr, "\twriting offset and signature to output stream...\n");
#endif
    sprintf(footerCumulativeRecordSizeBuffer, "%020llu", (unsigned long long) cumulativeRecSize); /* size_t cast to unsigned long long to avoid compilation warnings from ISO C++ compiler */
#ifdef DEBUG
    fprintf(stderr, "\tfooterCumulativeRecordSizeBuffer: %s\n", footerCumulativeRecordSizeBuffer);
#endif
    memcpy(footerBuffer, footerCumulativeRecordSizeBuffer, strlen(footerCumulativeRecordSizeBuffer));
    memcpy(footerBuffer + STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH, base64EncodedSha1Digest, STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1); /* strip trailing null */
    memset(footerRemainderBuffer, STARCH2_MD_FOOTER_REMAINDER_UNUSED_CHAR, (size_t) STARCH2_MD_FOOTER_REMAINDER_LENGTH);
#ifdef DEBUG
    fprintf(stderr, "\tfooterRemainderBuffer: [%s]\n", footerRemainderBuffer);
#endif
    memcpy(footerBuffer + STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1, footerRemainderBuffer, STARCH2_MD_FOOTER_REMAINDER_LENGTH); /* don't forget to offset pointer index by -1 for base64-sha1's null */
    footerBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1 + STARCH2_MD_FOOTER_REMAINDER_LENGTH - 1] = '\0';
    footerBuffer[STARCH2_MD_FOOTER_CUMULATIVE_RECORD_SIZE_LENGTH + STARCH2_MD_FOOTER_BASE64_ENCODED_SHA1_LENGTH - 1 + STARCH2_MD_FOOTER_REMAINDER_LENGTH - 2] = '\n';
    fprintf(stdout, "%s", footerBuffer);
    fflush(stdout);

    if (json)
        free(json), json = NULL;
    if (compressedFn)
        free(compressedFn), compressedFn = NULL;
    if (prevChromosome)
        free(prevChromosome), prevChromosome = NULL;
    if (base64EncodedSha1Digest)
        free(base64EncodedSha1Digest), base64EncodedSha1Digest = NULL;

    return STARCH_EXIT_SUCCESS;
}

int
STARCH2_writeStarchHeaderToOutputFp(const unsigned char *header, const FILE *outFp)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_writeStarchHeaderToOutputFp() ---\n");
    fprintf(stderr, "\theader: %s\n", header);
#endif

    if (!outFp) {
        fprintf(stderr, "ERROR: No output file pointer available to write starch header.\n");
        return STARCH_EXIT_FAILURE;
    }

    if (fwrite(header, STARCH2_MD_HEADER_BYTE_LENGTH, 1, (FILE *)outFp) != 1) {
        fprintf(stderr, "ERROR: Could not write all of starch header items to output file pointer.\n");
        return STARCH_EXIT_FAILURE;
    }

    return STARCH_EXIT_SUCCESS;
}

int
STARCH2_initializeStarchHeader(unsigned char **header) 
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_initializeStarchHeader() ---\n");
#endif
    int idx;

    *header = (unsigned char *) malloc (STARCH2_MD_HEADER_BYTE_LENGTH);
    if (!*header) {
        fprintf(stderr, "ERROR: Could not allocate space for header.\n");
        return STARCH_EXIT_FAILURE;
    }
    memset(*header, 0, STARCH2_MD_HEADER_BYTE_LENGTH);

    for (idx = 0; idx < STARCH2_MD_HEADER_BYTE_LENGTH; idx++) {
        if (idx < 4)
            (*header)[idx] = starchRevision2HeaderBytes[idx];
    }

    return STARCH_EXIT_SUCCESS;
}

void
STARCH2_printStarchHeader(const unsigned char *header)
{
#ifdef DEBUG
    fprintf(stderr, "\n--- STARCH2_printStarchHeader() ---\n");
#endif
    int idx;

    fprintf(stderr, "ERROR: Archive header:\n\t");
    for (idx = 0; idx < STARCH2_MD_HEADER_BYTE_LENGTH; idx++) {
        fprintf(stderr, "%02x", header[idx]);
        if ( ((idx + 1) % 4 == 0) && (idx != STARCH2_MD_HEADER_BYTE_LENGTH - 1) )
            fprintf(stderr, " ");
    }
    fprintf(stderr, "\n");
}