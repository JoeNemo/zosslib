#ifdef METTLE 
#include <metal/metal.h>
#include <metal/stddef.h>
#include <metal/stdio.h>
#include <metal/stdlib.h>
#include <metal/string.h>
#include <metal/stdarg.h>  
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  
#include "pthread.h"
#endif

#include "zowetypes.h"
#include "alloc.h"
#include "utils.h"
#include "zos.h"
#include "le.h"
#include "recovery.h"
#include "printf.h"
#include "intertypes.h"
#include "zossutil.h"
#include "debuggenerics.h"

#include "zosserror.h"


#define EXCEPTION_BUFFER_SIZE 1024

typedef struct ZMException_tag{
  char buffer[EXCEPTION_BUFFER_SIZE];
} ZMException;

int raiseException(char *formatString, ...){
  va_list args;
  ZMException *exception = (ZMException*)safeMalloc(sizeof(ZMException),"Exception");
  
  va_start(args, formatString);
  vsnprintf(exception->buffer, EXCEPTION_BUFFER_SIZE, formatString, args);
  va_end(args);
  printf("********* PANIC ********");
  printf("%s\n",exception->buffer);
  char *fakePointer = (char*)(0x80000000);
  return (int)fakePointer[13];   /* this had better generate a memory failure */
}

void inaccessibleMemoryWarning(RecoveryContext * __ptr32 context,
			       SDWA * __ptr32 sdwa,
			       void * __ptr32 userData) {
  printf("Could not get memory for address in register\n");
}

static int errorPageGetter(char *buffer, uint64_t pageAddress, void *pageGetterContext, int pageCount){
  /* printf("TPG pageAddr=0x%llx count=%d\n",pageAddress,pageCount); */
  memcpy(buffer,(char*)pageAddress,pageCount*0x1000);
  return 0;
}

typedef void ErrorDetailFunction(uint64_t instructionAddress,
				 uint64_t bearAddress,
				 SDWA * __ptr32 sdwa,
				 SDWAARC4 *sdwaarc4);

static ErrorDetailFunction *errorDetailFunction = NULL;

void setErrorDetailFunction(ErrorDetailFunction *f){
  errorDetailFunction = f;
}

typedef struct AbendParms_tag{
  RecoveryContext * __ptr32 context;
  SDWA * __ptr32 sdwa;
  void * __ptr32 userData;
  int  *ecb;
} AbendParms;

static void extractABENDInfoInternal(RecoveryContext * __ptr32 context,
				     SDWA * __ptr32 sdwa,
				     void * __ptr32 userData) {
  ABENDInfo *info = (ABENDInfo *)userData;

  if (sdwa){
    int reason = 0;
    
    int cc = (sdwa->flagsAndCode >> 12) & 0x00000FFF;
    int flags = (sdwa->flagsAndCode >> 24) & 0x000000FF;
    SDWAPTRS *sdwaptrs = NULL;
    SDWAARC4 *sdwaarc4 = NULL;
    SDWAARC1 *sdwaarc1 = NULL;

    if (flags & 0x04) {
      char *sdwadata = (char*)sdwa;
      sdwaptrs = (SDWAPTRS *)(sdwa->sdwaxpad);
      if (sdwaptrs != NULL) {
	char *sdwarc1 = (char *)sdwaptrs->sdwasrvp;
	if (sdwarc1 != NULL) {
	  reason = *(int * __ptr32)(sdwarc1 + 44);
	}
	sdwaarc4 = (SDWAARC4*)(sdwaptrs->sdwaxeme);
	sdwaarc1 = (SDWAARC1*)(sdwaptrs->sdwasrvp);
      }
    }
    printf("SDWA Code 0x%03x, reason=0x%x flags=0x%03x\n",cc,reason,flags);
    printf("TCB:  0x%08x\n",*((int*)CURRENT_TCB));
    printf("PIC:  0x%04X\n",(uint16_t)sdwa->sdwaintp);
    printf("EC1:  0x%016llx\n",(uint64_t)sdwa->sdwaec1);
    printf("EC2:  0x%016llx\n",(uint64_t)sdwa->sdwaec2);
    uint64_t bearAddress = 0;
    if (sdwaarc4){
      bearAddress = sdwaarc4->sdwabea;
      printf("BEA:  0x%llx\n",sdwaarc4->sdwabea);
    }
    for (int i=0; i<16; i++){
      if (sdwaarc4){
	printf("R%2d: %016llX ",i,(sdwaarc4->gprs[i]));
      } else{
	printf("R%2d: __xxxx__%08X ",i,(sdwa->lowGPRs[i]));
      }
      if ((i % 4) == 3){
	printf("\n");
      }
    }
    if (sdwaarc1){
      printf("SDWAARC1 (has CR's and AR's)\n");
      dumpbuffer((char*)sdwaarc1,0x118);
    }
    int codeWindowSize = 0x200;
    /* do things that touch memory after dumping anything of value from the SDWA */
    uint64_t failedInstructionAddress = (sdwa->sdwaec1&0x7FFFFFFF);
    char *codePointer = (char*)(failedInstructionAddress-(codeWindowSize/2));  /* ugly 31-bit assumption here */
    printf("Code near PSW at 0x%p\n",codePointer);
    dumpbuffer(codePointer,codeWindowSize);
    if (*((uint32_t*)codePointer) == 0x00C300C5){
      int  ppa1Offset = *((int*)(codePointer+8));
      printf("ppa1Offset = 0x%x\n",ppa1Offset);
      char *ppa1Pointer = codePointer+ppa1Offset;
      dumpbuffer(ppa1Pointer,0x60);
    }

    /*
      int wasProblem = supervisorMode(TRUE);
      ASCB *ascb = getASCB();
      char *debugArea = (char*)DEBUGAREA(ascb);
      printf("JOE WUZ HERE debugarea at 0x%0x\n",debugArea);
      dumpbuffer(debugArea,0x100);
      */
      
    /* char *remoteControlBlock = getLastRemoteControlBlock(); */

    /* 
    char *parmListGlobal = getLastIEAMSCHDParmList();
    printf("parmList at 0x%p\n",parmListGlobal);
    dumpbuffer(parmListGlobal,0x48);
    */
    for (int i=0; i<16; i++){
      char *registerAsPointer = (sdwaarc4 ? 
				 (char*)(sdwaarc4->gprs[i]) :
				 (char*)(sdwa->lowGPRs[i]) );
      if (0){ /* registerAsPointer >= (char*)0x2000){ */
	printf("data near R%d=0x%p\n",i,registerAsPointer);
	ABENDInfo memoryAbendInfo = {ABEND_INFO_EYECATCHER};
	/* display register memory, but need to guard */
	RecoveryContext *recoveryContext = (RecoveryContext*)getRecoveryContext();
	printf("recoveryContext=0x%p\n",recoveryContext);
	int recoveryRC = recoveryPush("Experiment1:abendDisplay",
				      RCVR_FLAG_NONE,
				      "Experiment1.2",
				      inaccessibleMemoryWarning, &memoryAbendInfo,
				      NULL, NULL);
	dumpbuffer(registerAsPointer,0x20);
	if (recoveryRC == RC_RCV_OK){
	  recoveryPop();
	}
      }
    }

    if (errorDetailFunction){
      errorDetailFunction((uint64_t)failedInstructionAddress,bearAddress,sdwa,sdwaarc4);
    }

  } else{
    printf("No SDWA for ABEND, this is going to be some tough sledding.\n");
  }
}

static int runExtractABENDInfo(RLETask *task){
  AbendParms *abendParms = (AbendParms*)task->userPointer;
  extractABENDInfoInternal(abendParms->context,abendParms->sdwa,abendParms->userData);
  zosPost(abendParms->ecb,0);
}

static void *runExtractABENDInfo_p(void *arg){
  printf("got into new debug thread!!\n");fflush(STDOUT);
  AbendParms *abendParms = (AbendParms*)arg;
  extractABENDInfoInternal(abendParms->context,abendParms->sdwa,abendParms->userData);
  pthread_exit(NULL);
}

void extractABENDInfo_simple(RecoveryContext * __ptr32 context,
			     SDWA * __ptr32 sdwa,
			     void * __ptr32 userData) {
  extractABENDInfoInternal(context,sdwa,userData);
}


void extractABENDInfo_alt(RecoveryContext * __ptr32 context,
		       SDWA * __ptr32 sdwa,
		       void * __ptr32 userData) {
  RLEAnchor *rleAnchor = makeRLEAnchor();
  initRLEEnvironment(rleAnchor);
  AbendParms *parms = (AbendParms*)safeMalloc(sizeof(AbendParms),"AbendParams");
  parms->context = context;
  parms->sdwa = sdwa;
  parms->userData = userData;
  RLETask *task0 = makeRLETask(rleAnchor,
			       RLE_TASK_TCB_CAPABLE,
			       runExtractABENDInfo);
  task0->userPointer = parms;
  int ecb = 0;
  parms->ecb = &ecb;
  startRLETask(task0,&ecb);
  zosWait(&ecb,0);
  printf("waiting cleared\n");

}

#ifndef METTLE
void extractABENDInfo(RecoveryContext * __ptr32 context,
		      SDWA * __ptr32 sdwa,
		      void * __ptr32 userData) {
  /* 
  __asm(ASM_PREFIX
	" ABEND 799 \n"
	:::"r15");
	*/
  ABENDInfo *abendInfo = (ABENDInfo*)userData;
  AbendParms *parms = (AbendParms*)(&abendInfo->closetSpace[0]);
  parms->context = context;
  parms->sdwa = sdwa;
  parms->userData = userData;
  pthread_t analysisThread;
  int rc = pthread_create(&analysisThread, NULL, runExtractABENDInfo_p, parms);
  if (rc){
    printf("could not spawn analysis thread\n");
    return;
  }
  pthread_join(analysisThread,NULL);
}
#endif
