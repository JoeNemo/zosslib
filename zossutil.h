#ifndef __ZOSSTEAM_DEBUGUTIL__
#define __ZOSSTEAM_DEBUGUTIL__ 1

#include "printf.h"

/* ArrayLists */

#ifndef __LONGNAME__

#define makeArrayList ALSTMAKE
#define arrayListAdd  ALSTADD
#define arrayListElement ALSTELMT
#define arrayListSort ALSTSORT
#define arrayListShallowCopy ALSHLCPY 
#define initEmbbededArrayList ALINEMAR

#define makeByteOutputStream BOSMAKE
#define bosWrite BOSWRIT
#define bosAppendString BOSAPST
#define bosAppendChar BOSAPCH
#define bosReset BOSRSET
#define bosNullTerminateAndUse BOSNTUS
#define bosUse  BOSUSE
#define bosFree BOSFREE


#endif


typedef struct ArrayList_tag{
  int capacity;
  int size;
  void **array;
  ShortLivedHeap *slh;
} ArrayList;

ArrayList *makeArrayList();
void arrayListFree(ArrayList *list);
void arrayListAdd(ArrayList *list, void *thing);
void *arrayListElement(ArrayList *list, int i);
void initEmbeddedArrayList(ArrayList *list,
			   ShortLivedHeap *slh);
void *arrayListShallowCopy(ArrayList *source, ArrayList *target);
void arrayListSort(ArrayList *list, int (*comparator)(void *a, void *b));

/* Variable-length output buffering */

typedef struct ByteOutputStream_tag{
  int   size;
  int   capacity;
  int   chunkSize;
  char *data;
} ByteOutputStream;

ByteOutputStream *makeByteOutputStream(int chunkSize);
/* 
   returns current size 
   */
int  bosWrite(ByteOutputStream *bos, char *data, int dataSize);
/* 
   Without changing backing memory, reset the output postion to 0.
   */
int bosAppendString(ByteOutputStream *bos, char *s);
int bosAppendChar(ByteOutputStream *bos, char c);
void bosReset(ByteOutputStream *bos);
char *bosNullTerminateAndUse(ByteOutputStream *bos);
char *bosUse(ByteOutputStream *bos);
void bosFree(ByteOutputStream *bos, bool freeBuffer);

/* these utilities are Dataset-friendly, ie. they support filenames
   that start with the "//" syntax to refer to traditional MVS
   cataloged dataset names and the //'PDS.NAME(MEMBER)' syntax 
   */
bool fileExists(char *filename);
char *findPathOnConcatenation(ArrayList *concatenation,
			      char *filenameStem, char *unixExtension,
			      char *buffer, int bufferLength);
char *readWholeFile(char *filename, int *length);

char *ebcdicNToNative(char *s, int len);
char *ebcdicInNative(char *s, char *scratchBuffer, int bufferLength);

char *copyStringOnHeap(char *str);


#endif
