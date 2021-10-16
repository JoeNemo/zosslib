#ifdef METTLE 
#include <metal/metal.h>
#include <metal/stddef.h>
#include <metal/stdio.h>
#include <metal/stdlib.h>
#include <metal/string.h>
#include <metal/stdarg.h>  
#include <metal/ctype.h>  
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  
#include <ctype.h>
#endif

#include "zowetypes.h"
#include "alloc.h"
#include "utils.h"
#include "zos.h"
#include "le.h"
#include "unixfile.h"
#include "recovery.h"
#include "zossutil.h"

char *copyStringOnHeap(char *str){
  int len = strlen(str);
  char *copy = safeMalloc(len+1,"copystringonheap");
  memcpy(copy,str,len);
  copy[len] = 0;
  return copy;
}

char *ebcdicNToNative(char *s, int len){
  char *translated = safeMalloc(len+1,"EBCDIC To Native");
  /* uint8_t *ebcdicTable = getCp1047ToISO8859(); */
  for (int i=0; i<len; i++){
    translated[i] = s[i];
    /* printf("0x%x -> 0x%x\n",(int)s[i],(int)translated[i]); */
  }
  translated[len] = 0;
  return translated;
}

/* used to help printing/tracing code */
char *ebcdicInNative(char *s, char *scratchBuffer, int bufferLength){
  int len = strlen(s);
  int lim = (len < bufferLength-1) ? len : bufferLength-1;
  /* uint8_t *ebcdicTable = getCp1047ToISO8859(); */
  for (int i=0; i<len; i++){
    scratchBuffer[i] = s[i];
  }
  scratchBuffer[lim] = 0;
  return scratchBuffer;
}

static void *arrayListAlloc(ArrayList *list, uint32_t size, char *location){
  if (list->slh){
    return (void*)SLHAlloc(list->slh,size);
  } else{
    return safeMalloc(size,location);
  }
}

ArrayList *makeArrayList(){
  ArrayList *list = (ArrayList*)safeMalloc(sizeof(ArrayList),"ArrayList");
  list->capacity = 8;
  list->size     = 0;
  list->array = (void**)safeMalloc(list->capacity*sizeof(void*),"ArrayListArray");
  list->slh = NULL;
  return list;
}

void arrayListFree(ArrayList *list){
  if (list->slh == NULL){
    safeFree((char*)list->array,list->capacity*sizeof(void*));
    safeFree((char*)list,sizeof(ArrayList));
  }
}

void initEmbeddedArrayList(ArrayList *list,
			   ShortLivedHeap *slh){ /* can be null for use safeMalloc - standard heap alloc */
  list->capacity = 8;
  list->size     = 0;
  list->slh      = slh;
  list->array = (void**)arrayListAlloc(list,list->capacity*sizeof(void*),"ArrayListArray");  
}

void arrayListAdd(ArrayList *list, void *thing){
  if (list->size == list->capacity){
    int newCapacity = 2*list->capacity;
    void** newArray = (void**)arrayListAlloc(list,newCapacity*sizeof(void*),"ArrayListExtend");
    memcpy(newArray,list->array,list->capacity*sizeof(void*));
    if (list->slh == NULL){
      safeFree((char*)list->array,list->capacity*sizeof(void*));
    }
    list->array = newArray;
    list->capacity = newCapacity;
  }
  list->array[list->size++] = thing;
}

void arrayListSort(ArrayList *list, int (*comparator)(void *a, void *b)){
  qsort(list->array,list->size,sizeof(void*),comparator);
}

bool arrayListContains(ArrayList *list, void *element){
  for (int i=0; i<list->size; i++){
    if (list->array[i] == element){
      return true;
    }
  }
  return false;
}

void *arrayListElement(ArrayList *list, int i){
  if (i<list->size){
    return list->array[i];
  } else{
    return NULL;
  }
}

void *arrayListShallowCopy(ArrayList *source, ArrayList *target){
  target->capacity = source->capacity;
  target->size     = source->size;
  target->slh      = source->slh;
  target->array = (void**)arrayListAlloc(target,target->capacity*sizeof(void*),"ArrayListArray");  
  memcpy(target->array,source->array,target->capacity*sizeof(void**));
  return target;
}

ByteOutputStream *makeByteOutputStream(int chunkSize){
  ByteOutputStream *bos = (ByteOutputStream*)safeMalloc(sizeof(ByteOutputStream),"ByteOutputStream");
  bos->size = 0;
  bos->capacity = chunkSize;
  bos->chunkSize = chunkSize;
  bos->data = safeMalloc(chunkSize,"ByteOutputStream Chunk");
  return bos;
}

int bosWrite(ByteOutputStream *bos, char *data, int dataSize){
  if (bos->size+dataSize > bos->capacity){
    int extendSize = (bos->chunkSize > dataSize) ? bos->chunkSize : dataSize;
    printf("bos extend currSize=0x%x dataSize=0x%x chunk=0x%x extend=0x%x\n",
           bos->size,dataSize,bos->chunkSize,extendSize);
    int newCapacity = bos->capacity + extendSize;
    char *newData = safeMalloc(newCapacity,"BOS extend");
    memcpy(newData,bos->data,bos->size);
    safeFree(bos->data,bos->capacity);
    bos->data = newData;
    bos->capacity = newCapacity;
  }
  memcpy(bos->data+bos->size,data,dataSize);
  bos->size += dataSize;
  return bos->size;
}

int bosAppendString(ByteOutputStream *bos, char *s){
  return bosWrite(bos,s,strlen(s));
}

int bosAppendChar(ByteOutputStream *bos, char c){
  return bosWrite(bos,&c,1);
}

char *bosNullTerminateAndUse(ByteOutputStream *bos){
  char c = 0;
  bosWrite(bos,&c,1);
  return bos->data;
}

char *bosUse(ByteOutputStream *bos){
  return bos->data;
}

void bosReset(ByteOutputStream *bos){
  bos->size = 0;
}

void bosFree(ByteOutputStream *bos, bool freeBuffer){
  if (freeBuffer){
    safeFree(bos->data,bos->capacity);
  }
  safeFree((char*)bos,sizeof(ByteOutputStream));
}

bool fileExists(char *filename){
#ifndef METTLE
  BPXYSTAT stats;
  int returnCode = 0;
  int reasonCode = 0;
  if (!memcmp(filename,"//",2)){
    FILE *in = fopen(filename,"rb");
    if (in){
      fclose(in);
      return true;
    } else{
      return false;
    }
  } else{
    fileInfo(filename,&stats,&returnCode,&reasonCode);
    if (returnCode != 0){
      return false;
    } else{
      return true;
    }
  }
#else
  return false;   /* hack to make plugin build work */
#endif 
}

char *findPathOnConcatenation(ArrayList *concatenation,
			      char *filenameStem, char *unixExtension,
			      char *buffer, int bufferLength){
  for (int i=0; i<concatenation->size; i++){
    char *pathElement = (char*)arrayListElement(concatenation,i);
    if (!memcmp(pathElement,"//",2)){  /* PDS/PDSE case */
      int position = snprintf(buffer,bufferLength,"%s",pathElement);
      int stemLength = strlen(filenameStem);
      buffer[position] = '(';
      for (int i=0; i<stemLength; i++){
	buffer[position++] = toupper(filenameStem[i]);
      }
      buffer[position++] = ')';
      buffer[position++] = '\'';
      buffer[position++] = 0;
    } else{
      snprintf(buffer,bufferLength,"%s/%s%s",pathElement,filenameStem,unixExtension);
    }
    printf("about to test file for existence '%s'\n",buffer);
    if (fileExists(buffer)){
      return buffer;
    }
  }
  return NULL;
}


#define MAX_FILE_SIZE 100000000

static char *readWholeUnixFile(char *filename, int *length){
  BPXYSTAT stats;
  int returnCode = 0;
  int reasonCode = 0;
  fileInfo(filename,&stats,&returnCode,&reasonCode);
  if (returnCode != 0){
    printf("could not get stats\n");
    return NULL;
  }
  int fileLength = (int)stats.fileSize;
  if (fileLength < MAX_FILE_SIZE){
    char *data = safeMalloc(fileLength,"POData");
    *length = fileLength;
    UnixFile *in = fileOpen(filename,FILE_OPTION_READ_ONLY,0,0,&returnCode,&reasonCode);
    if (in == NULL){
      printf("could not open %s , return=%d reason=0x%x\n",filename,returnCode,reasonCode);
      return NULL;
    }
    int bytesRead = fileRead(in,data,fileLength,&returnCode,&reasonCode);
    if (returnCode){
      printf("read failed, return=%d reason=0x%x\n",filename,returnCode,reasonCode);
    }
    if (bytesRead != fileLength){
      printf("could not read fully\n");
    } else{
      /* printf("bytesRead=%d\n",bytesRead); */
    }
    fileClose(in,&returnCode,&reasonCode);
    return data;
  } else{
    return NULL;
  }
}

#define READ_BUFFER_SIZE 0x1000
#define CHUNK_SIZE (0x10*READ_BUFFER_SIZE)

#ifndef METTLE
static char *readWholeFile2(char *filename, int *length){
  FILE *in = fopen(filename,"rb");
  if (in == NULL){
    return NULL;
  }
  int dataSize = 0;
  int dataPos = 0;
  char *data = NULL;
  while (!feof(in)){
    if (dataPos+READ_BUFFER_SIZE > dataSize){
      int newDataSize = dataSize + CHUNK_SIZE;
      char *newData = safeMalloc(newDataSize,"Whole File Chunk");
      if (data){
	memcpy(newData,data,dataPos);
	safeFree(data,dataSize);
      }
      dataSize = newDataSize;
      data = newData;
    }
    printf("data is 0x%p size=0x%x\n",data,dataSize);
    int bytesRead = fread(data+dataPos,1,READ_BUFFER_SIZE,in);
    if (bytesRead > 0){
      dataPos += bytesRead;
    }
  }
  fclose(in);
  *length = dataPos;
  return data;
}
#endif

char *readWholeFile(char *filename, int *length){
  if (memcmp(filename,"//",2)){
    return readWholeUnixFile(filename,length);
  } else{
#ifndef METTLE
    return readWholeFile2(filename,length);
#else
    return NULL;
#endif
  }
}
