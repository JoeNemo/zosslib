#ifndef __ZOSSTEAM_INZERROR__
#define __ZOSSTEAM_INZERROR__

typedef struct ABENDInfo_tag {
  char eyecatcher[8];
#define ABEND_INFO_EYECATCHER "ABENDNFO"
  int completionCode;
  int reasonCode;
  char closetSpace[0x100];
} ABENDInfo;

typedef struct SDWAARC1_tag{
  char    blob[0x1C8];   /* Access Registers start at D8 */
} SDWAARC1;

void inaccessibleMemoryWarning(RecoveryContext * __ptr32 context,
			       SDWA * __ptr32 sdwa,
			       void * __ptr32 userData);

#define DEBUGAREA(ascb) *((uint32_t*)(((char*)ascb)+0x114))

void extractABENDInfo(RecoveryContext * __ptr32 context,
		      SDWA * __ptr32 sdwa,
		      void * __ptr32 userData);


#endif
