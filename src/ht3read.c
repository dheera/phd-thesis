// File: ht3read.c
//
// Hydraharp ht3 reader
// Based on sample Borland C code by PicoQuant
// Translated to GNU C by Dheera Venkatraman
// with additional features added for first-photon imaging
//
// Usage: ht3read [options] infile.ht3 outfile.txt
//
// Options:
// -nmax N       Only output N photon detections after each marker
// -tmin TMIN    Enable range-gating with minimum time bin TMIN
// -tmax TMAX    Enalbe range-gating with maximum time bin TMAX
// +s, -s        Enable/disable printing of arrivals before the first marker
// +m, -m        Print markers
// +o, -o        Print overflows
// +h, -h        Print headers with device and acquisition information

#include<stdio.h>
#include<string.h>
#include<stddef.h>
#include<stdlib.h>
#include<inttypes.h>

#define DISPCURVES 8	// not relevant in TT modes but needed in file header definition
#define MAXINPCHANS 8
#define T3WRAPAROUND 1024

#pragma pack(8) //structure alignment to 8 byte boundaries

typedef struct {
  float Start;
  float Step;
  float End;
} tParamStruct;

typedef struct {
  int MapTo;
  int Show;
} tCurveMapping;

typedef	struct {
  int ModelCode;
  int VersionCode;
} tModuleInfo;

typedef union {
  uint32_t allbits;
  struct  {
    unsigned nsync:10;  // number of sync periods
    unsigned dtime:15;  // delay from last sync in units of chosen resolution 
    unsigned channel:6; // detection channel
    unsigned special:1;
  } bits;
} tT3Rec;


// The following represents the readable ASCII file header portion. 

struct {
  char Ident[16];          // "HydraHarp"
  char FormatVersion[6];   // file format version
  char CreatorName[18];	   // acquisition software name
  char CreatorVersion[12]; // acquisition software version
  char FileTime[18];
  char CRLF[2];
  char CommentField[256];
} TxtHdr;

// The following is binary file header information indentical to that in HHD files.
// Note that some items are not meaningful in the time tagging modes.

struct {
  int Curves;
  int BitsPerRecord;  // data of one event record has this many bits
  int ActiveCurve;
  int MeasMode;
  int SubMode;
  int Binning;			
  double Resolution;  // in ps
  int Offset;				
  int Tacq;           // in ms
  int StopAt;
  int StopOnOvfl;
  int Restart;
  int DispLinLog;
  int DispTimeFrom;   // 1 ns steps
  int DispTimeTo;
  int DispCountsFrom;
  int DispCountsTo;
  tCurveMapping DispCurves[DISPCURVES];	
  tParamStruct Params[3];
  int RepeatMode;
  int RepeatsPerCurve;
  int RepeatTime;
  int RepeatWaitTime;
  char ScriptName[20];
} BinHdr;

// Hardware information header

struct {		
  char HardwareIdent[16];   
  char HardwarePartNo[8]; 
  int  HardwareSerial; 
  int  nModulesPresent;     
  tModuleInfo ModuleInfo[10]; // upto 10 modules can be combined into a single file
  double BaseResolution;
  unsigned long long int InputsEnabled; // a bitfield
  int InpChansPresent;  // determines the number of ChannelHeaders below!
  int RefClockSource;
  int ExtDevices;     // a bitfield
  int MarkerSettings; // a bitfield
  int SyncDivider;
  int SyncCFDLevel;
  int SyncCFDZeroCross;
  int SyncOffset;
} MainHardwareHdr;

// How many of the following array elements are actually present in the file 
// is indicated by InpChansPresent above. Here we allocate the possible maximum.

struct { 
  int InputModuleIndex; // module that corresponds to current channel
  int InputCFDLevel;
  int InputCFDZeroCross; 
  int InputOffset;
} InputChannelSettings[MAXINPCHANS]; 

// Up to here the header was identical to that of HHD files.
// The following header sections are specific for the TT modes 

// How many of the following array elements are actually present in the file 
// is indicated by InpChansPresent above. Here we allocate the possible maximum.

int InputRate[MAXINPCHANS];

// the following exists only once				

struct {	
  int SyncRate;
  int StopAfter;
  int StopReason;
  int ImgHdrSize;
  unsigned long long int nRecords;
} TTTRHdr;

// how many of the following ImgHdr array elements are actually present in the file 
// is indicated by ImgHdrSize above. 
// Storage must be allocated dynamically if ImgHdrSize other than 0 is found.
//				int ImgHdr[ImgHdrSize];

// The headers end after ImgHdr. Following in the file are only event records. 
// How many of them actually are in the file is indicated by nRecords in TTTRHdr above.

int main(int argc, char* argv[]) {
  int result;
  FILE* fpin;
  FILE* fpout; 	
  int i;
  tT3Rec T3Rec;
  unsigned long long int lastmarker[20]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned long long int n, truensync=0, oflcorrection = 0;

  unsigned long long int marker_total=0;
  unsigned long long int dtime_total=0;

  int print_headers=0;  // print headers or not
  int print_overflow=0; // print overflows or not
  int print_marker=1;   // print markers or not
  int print_start=0;    // print counts arriving before first marker?
  long long int print_nmax=-1;   // print first N counts after each marker (-1 for all)
  long long int print_n=-1;
  char infile[512];
  char outfile[512];

  long long int print_tmin=-1; // range-gating minimum time bin index
  long long int print_tmax=-1; // range-gating maximum time bin index

  unsigned long long int total_m=0; // total number of markers
  unsigned long long int total_o=0; // total number of overflows
  unsigned long long int total_c=0; // total number of counts

  if(argc<3) {
    fprintf(stderr,"Usage: ht3read [options] infile.ht3 oufile.txt\n\n");
    fprintf(stderr,"Options:\n");
    fprintf(stderr,"-nmax N       Only output N photon detections after each marker\n");
    fprintf(stderr,"-tmin TMIN    Enable range-gating with minimum time bin TMIN\n");
    fprintf(stderr,"-tmax TMAX    Enalbe range-gating with maximum time bin TMAX\n");
    fprintf(stderr,"+s, -s        Enable/disable printing of arrivals before the first marker\n");
    fprintf(stderr,"+m, -m        Print markers\n");
    fprintf(stderr,"+o, -o        Print overflows\n");
    fprintf(stderr,"+h, -h        Print headers with device and acquisition information\n");
    exit(-1);
  }

  for(i=1;i<argc-2;i++) {
    if(strcmp(argv[i-1],"-nmax")==0) {
      print_nmax=atoi(argv[i]);
      if(print_nmax<-1) {
        fprintf(stderr,"error: number of counts out of bounds\n");
        exit(1);
      }
      fprintf(stdout,"(Printing only %lld counts per marker)\n",print_nmax);
    }
    else if(strcmp(argv[i],"-nmax")==0) {}
    else if(strcmp(argv[i-1],"-tmin")==0) {
      print_tmin=atoi(argv[i]);
      if(print_tmin<-1) {
        fprintf(stderr,"error: tmin out of bounds\n");
        exit(1);
      }
      fprintf(stdout,"(Printing only time bins >= %lld)\n",print_tmin);
    }
    else if(strcmp(argv[i],"-tmin")==0) {}
    else if(strcmp(argv[i-1],"-tmax")==0) {
      print_tmax=atoi(argv[i]);
      if(print_tmax<-1) {
        fprintf(stderr,"error: tmax out of bounds\n");
        exit(1);
      }
      fprintf(stdout,"(Printing time bins <= %lld)\n",print_tmax);
    }
    else if(strcmp(argv[i],"-tmax")==0) {}
    else if(strcmp(argv[i],"-h")==0) print_headers=0;
    else if(strcmp(argv[i],"+h")==0) print_headers=1;
    else if(strcmp(argv[i],"-s")==0) print_start=0;
    else if(strcmp(argv[i],"+s")==0) print_start=1;
    else if(strcmp(argv[i],"-m")==0) print_marker=0;
    else if(strcmp(argv[i],"+m")==0) print_marker=1;
    else if(strcmp(argv[i],"-o")==0) print_overflow=0;
    else if(strcmp(argv[i],"+o")==0) print_overflow=1;
    else {
      fprintf(stderr,"error: invalid option %s\n",argv[i]);
      exit(1);
    }
  }

  if((fpin=fopen(argv[argc-2],"rb"))==NULL) {
    fprintf(stderr,"error: input file cannot be opened, aborting.\n");
    exit(1);
  }
	
  if((fpout=fopen(argv[argc-1],"w"))==NULL) {
    fprintf(stderr,"error: output file cannot be opened, aborting.\n");
    exit(1);
  }

  fprintf(stdout,"Loading data from %s ...\n", argv[argc-2]);
  fprintf(stdout,"Writing output to %s ...\n", argv[argc-1]);
  result = fread( &TxtHdr, 1, sizeof(TxtHdr) ,fpin);
  if(result!=sizeof(TxtHdr)) {
    fprintf(stderr,"error: cannot read text header\n");
    exit(1);
  }
  if(print_headers) {
    fprintf(fpout,"%% Ident             : %.*s\n",
            (int)sizeof(TxtHdr.Ident),TxtHdr.Ident);
    fprintf(fpout,"%% Format Version    : %.*s\n",
            (int)sizeof(TxtHdr.FormatVersion),TxtHdr.FormatVersion);
    fprintf(fpout,"%% Creator Name      : %.*s\n",
            (int)sizeof(TxtHdr.CreatorName),TxtHdr.CreatorName);
    fprintf(fpout,"%% Creator Version   : %.*s\n",
            (int)sizeof(TxtHdr.CreatorVersion),TxtHdr.CreatorVersion);
    fprintf(fpout,"%% Time of Creation  : %.*s\n",
            (int)sizeof(TxtHdr.FileTime),TxtHdr.FileTime);
    fprintf(fpout,"%% File Comment      : %.*s\n",
            (int)sizeof(TxtHdr.CommentField),TxtHdr.CommentField);
  }

  if(strncmp(TxtHdr.FormatVersion,"1.0",3)&&strncmp(TxtHdr.FormatVersion,"2.0",3)) {
    if(print_headers) {
      fprintf(stderr, 
              "error: File format version is %s. This program is for version 1.0 and 2.0 only.\n",
              TxtHdr.FormatVersion);
    }
    exit(1);
  }

  result = fread( &BinHdr, 1, sizeof(BinHdr) ,fpin);
  if(result!=sizeof(BinHdr)) {
    fprintf(stderr,"error: cannot read bin header, aborted.\n");
    exit(1);
  }
  if(print_headers) {
    fprintf(fpout,"%% Bits per Record   : %d\n",BinHdr.BitsPerRecord);
    fprintf(fpout,"%% Measurement Mode  : %d\n",BinHdr.MeasMode);
    fprintf(fpout,"%% Sub-Mode          : %d\n",BinHdr.SubMode);
    fprintf(fpout,"%% Binning           : %d\n",BinHdr.Binning);
    fprintf(fpout,"%% Resolution        : %lf\n",BinHdr.Resolution);
    fprintf(fpout,"%% Offset            : %d\n",BinHdr.Offset);
    fprintf(fpout,"%% AcquisitionTime   : %d\n",BinHdr.Tacq);
  }
  // Note: for formal reasons the BinHdr is identical to that of HHD files.  
  // It therefore contains some settings that are not relevant in the TT modes,
  // e.g. the curve display settings. So we do not write them out here.

  result = fread( &MainHardwareHdr, 1, sizeof(MainHardwareHdr) ,fpin);
  if(result!=sizeof(MainHardwareHdr)) {
    fprintf(stderr,"error: cannot read MainHardwareHdr, aborted.\n");
    exit(1);
  }

  if(print_headers) {
    fprintf(fpout,"%% HardwareIdent     : %.*s\n",
            (int)sizeof(MainHardwareHdr.HardwareIdent),MainHardwareHdr.HardwareIdent);
    fprintf(fpout,"%% HardwarePartNo    : %.*s\n",
            (int)sizeof(MainHardwareHdr.HardwarePartNo),MainHardwareHdr.HardwarePartNo);
    fprintf(fpout,"%% HardwareSerial    : %d\n",MainHardwareHdr.HardwareSerial);
    fprintf(fpout,"%% nModulesPresent   : %d\n",MainHardwareHdr.nModulesPresent);
  }


  // the following module info is needed for support enquiries only
  for(i=0;i<MainHardwareHdr.nModulesPresent;++i) {
    if(print_headers) {
      fprintf(fpout,"%% Moduleinfo[%02d]    : %08x %08x\n",
        i, MainHardwareHdr.ModuleInfo[i].ModelCode, MainHardwareHdr.ModuleInfo[i].VersionCode);
    }
  }

  //the following are important measurement settings
  if(print_headers) {
    fprintf(fpout,"%% BaseResolution    : %lf\n",MainHardwareHdr.BaseResolution);
    fprintf(fpout,"%% InputsEnabled     : %llu\n",MainHardwareHdr.InputsEnabled);
    fprintf(fpout,"%% InpChansPresent   : %d\n",MainHardwareHdr.InpChansPresent);
    fprintf(fpout,"%% RefClockSource    : %d\n",MainHardwareHdr.RefClockSource);
    fprintf(fpout,"%% ExtDevices        : %x\n",MainHardwareHdr.ExtDevices);
    fprintf(fpout,"%% MarkerSettings    : %x\n",MainHardwareHdr.MarkerSettings);
    fprintf(fpout,"%% SyncDivider       : %d\n",MainHardwareHdr.SyncDivider);
    fprintf(fpout,"%% SyncCFDLevel      : %d\n",MainHardwareHdr.SyncCFDLevel);
    fprintf(fpout,"%% SyncCFDZeroCross  : %d\n",MainHardwareHdr.SyncCFDZeroCross);
    fprintf(fpout,"%% SyncOffset        : %d\n",MainHardwareHdr.SyncOffset);
  }


  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
    if(print_headers) {
      fprintf(fpout,"%% ---------------------\n");
    }
      result = fread( &(InputChannelSettings[i]), 1, sizeof(InputChannelSettings[i]) ,fpin);
      if(result!=sizeof(InputChannelSettings[i])) {
        printf("\nerror reading InputChannelSettings, aborted.");
        exit(1);
      }
    if(print_headers) {
      fprintf(fpout,"%% Input Channel %1d\n",i);
      fprintf(fpout,"%%  InputModuleIndex  : %d\n",InputChannelSettings[i].InputModuleIndex);
      fprintf(fpout,"%%  InputCFDLevel     : %d\n",InputChannelSettings[i].InputCFDLevel);
      fprintf(fpout,"%%  InputCFDZeroCross : %d\n",InputChannelSettings[i].InputCFDZeroCross);
      fprintf(fpout,"%%  InputOffset       : %d\n",InputChannelSettings[i].InputOffset);
    }
  }

  if(print_headers) {
    fprintf(fpout,"%% ---------------------\n");
  }
  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
    result = fread( &(InputRate[i]), 1, sizeof(InputRate[i]) ,fpin);
    if(result!=sizeof(InputRate[i])) {
      fprintf(stderr,"error reading InputRates, aborted\n");
      exit(1);
    }
    if(print_headers) {
      fprintf(fpout,"%% Input Rate [%1d]     : %1d\n",i,InputRate[i]);
    }
  }

  if(print_headers) {
    fprintf(fpout,"%% ---------------------\n");
  }

  result = fread( &TTTRHdr, 1, sizeof(TTTRHdr) ,fpin);
  if(result!=sizeof(TTTRHdr)) {
    fprintf(stderr,"error: error reading TTTRHdr, aborted\n");
    exit(1);
  }

  if(print_headers) {
    fprintf(fpout,"%% SyncRate           : %d\n",TTTRHdr.SyncRate);
    fprintf(fpout,"%% StopAfter          : %d\n",TTTRHdr.StopAfter);
    fprintf(fpout,"%% StopReason         : %d\n",TTTRHdr.StopReason);
    fprintf(fpout,"%% ImgHdrSize         : %d\n",TTTRHdr.ImgHdrSize);
    fprintf(fpout,"%% nRecords           : %llu\n",TTTRHdr.nRecords);
    fprintf(fpout,"%% ---------------------\n");
  }

  // Imaging header only relevant for PicoQuant-proprietary imaging devices
  fseek(fpin,TTTRHdr.ImgHdrSize*4,SEEK_CUR);


  // Read and interpret the event records
  for(n=0;n<TTTRHdr.nRecords;n++) {
    i=0;  
    result = fread(&T3Rec.allbits,sizeof(T3Rec.allbits),1,fpin); 
    if(result!=1) {
      if(feof(fpin)==0) {
        fprintf(stderr,"error: error in input file\n");
        exit(1);
      }
    }

    if(T3Rec.bits.special==1) {
      if(T3Rec.bits.channel==0x3F) { // overflow
        if(print_overflow) {
          fprintf(fpout,"%llu 2 0 0 0\n", n);
          total_o++;
        }
        if(T3Rec.bits.nsync==0)
          oflcorrection+=T3WRAPAROUND;
        else
          oflcorrection+=T3WRAPAROUND*T3Rec.bits.nsync;
      }		
      if((T3Rec.bits.channel>=1)&&(T3Rec.bits.channel<=15)) { // marker
        truensync = oflcorrection + T3Rec.bits.nsync;
        // the time unit depends on sync period which can be obtained from the file header
        if(print_marker && (lastmarker[T3Rec.bits.channel]==0 ||
            (truensync>lastmarker[T3Rec.bits.channel] &&
              truensync-lastmarker[T3Rec.bits.channel]>5))) {
          fprintf(fpout,"%llu 0 %02x %llu %llu\n", n, T3Rec.bits.channel, marker_total, dtime_total);
          total_m++;
          marker_total=0;
          dtime_total=0;
          print_n=0;
        }
        lastmarker[T3Rec.bits.channel]=truensync;
      }
    } else { // regular input channel
      if((print_start||print_n!=-1) && (print_nmax==-1||print_n<print_nmax)) {
        truensync = oflcorrection + T3Rec.bits.nsync; 
        // the nsync time unit depends on sync period which can be obtained from the file header
        // the dtime unit depends on the resolution and can also be obtained from the file header
        if( (print_tmin==-1 || T3Rec.bits.dtime>=print_tmin)
         && (print_tmax==-1 || T3Rec.bits.dtime<=print_tmax)) {
          fprintf(fpout,"%llu 1 %02x %llu %u\n", n, T3Rec.bits.channel, truensync, T3Rec.bits.dtime);
          print_n++;
          total_c++;
        }
      }
      if( (print_tmin==-1 || T3Rec.bits.dtime>=print_tmin)
        && (print_tmax==-1 || T3Rec.bits.dtime<=print_tmax)) {
          dtime_total+=T3Rec.bits.dtime;
          marker_total++;
      }
    }
    if(n%100000==0) {
      fprintf(stdout,"[%llu%%] Total printed records: M %llu / O %llu / C %llu\r",
              100*n/TTTRHdr.nRecords,total_m,total_o,total_c);
      fflush(stdout);
    }
  }
  fprintf(stdout,"[100%%] Total printed records: M %llu / O %llu / C %llu\n\n",
          total_m,total_o,total_c);

  fclose(fpin);
  fclose(fpout); 

  exit(0);
  return(0);		
}
