// File: spadcounts.c
// 
// Data reader for 32x32 photon counting array from Politecnico di Milano
// Written by Dheera Venkatraman
//
// Usage: spadcounts [options] myfile.bin
//
// Options:
// -a            ASCII output
// -b            binary output (default)
// -o file.out   output to filename.out
// -s N          new output file every N frames
//
// Output:
//
// For ASCII mode: Text lines, one for each photon arrival, formatted as
// [frame number (0-16383N)] [pixel number (0-1024)] [time bin number (0-1024)]
//
// For binary mode: 64-bit records, one for each photon arrival, formatted as
// [frame number (32 bits)] [pixel number (16 bits)] [time bin number (16 bits)]
//
// (The pixel number and time bin number in principle need only 10 bits each but
// by byte-aligning records we are able to read files much faster in MATLAB.)

#include<stdio.h>
#include<string.h>
#include<stddef.h>
#include<stdlib.h>
#include<inttypes.h>

FILE* fopen64(const char *filename, const char *type);

typedef struct {
 unsigned int t[1024];
} spadFrame;

typedef union {
  uint16_t allbits;
  struct {
    unsigned short int coarse:6;
    unsigned short int fine:4;
    unsigned short int zero:6;
  } bits;
  struct {
    unsigned short int byte0:8;
    unsigned short int byte1:8;
  } bytes;
} spadRecord;

typedef union {
  uint16_t allbits[18];
  struct {
    uint16_t dummy0[3];
    unsigned short int stop:4;
    unsigned short int dummy1:4;
    unsigned short int dummy2:4;
    unsigned short int dummy3:4;
    uint16_t dummy4[14];
  } bits;
} spadStop;

const int FORMAT_ASCII = 0;
const int FORMAT_BINARY = 1;

int main(int argc, char* argv[]) {
  FILE* infile;
  FILE* outfile;
  int result;
  unsigned int i;
  unsigned long int j;
  char c;
  uint64_t outrec;

  spadRecord raw_records[1024];
  spadStop raw_stop;
  spadFrame current_frame;
  char outfile_name[1024];
  char outfile_name_stem[1024];
  unsigned long int options_splitfile = 0;
  int splitfile_count = 0;

  unsigned int pixel_indexes[1024];

  for(i=0;i<1024;i++) {
    if(i%2) {
      pixel_indexes[i] = ( 0b1111100000 ^ ( (31-(i/64))<<5 ) ) |
                         ( i/2 & 0b0000011111 );
    } else {
      pixel_indexes[i] = ( 0b1111100000 ^ ( (i+1)/2 & 0b1111100000 ) ) |
                         ( (i+1)/2 & 0b0000011111 );
    }
  }

  if(argc<2) {
    fprintf(stderr,"usage: spadcounts [options] infile.bin\n\n");
    fprintf(stderr,"options:\n");
    fprintf(stderr,"    -a            ASCII output\n");
    fprintf(stderr,"    -b            binary output (default)\n");
    fprintf(stderr,"    -o file.out   output to filename.out\n");
    fprintf(stderr,"    -s N          new output file every N frames\n\n");
    exit(-1);
  }

  short int options_format = FORMAT_BINARY;

  sprintf(outfile_name_stem, "%s.out", argv[argc-1]);

  for(i=1;i<argc-1;i++) {
    if(strcmp(argv[i-1],"-o")==0) {
      strcpy(outfile_name_stem, argv[i]);
    } if(strcmp(argv[i-1],"-s")==0) {
      options_splitfile = atol(argv[i]);
      if(options_splitfile < 0) {
        fprintf(stderr,"error: invalid parameter for -s: %s\n", argv[i]);
        exit(1);
      }
    } else if(strcmp(argv[i],"-a")==0) {
      options_format = FORMAT_ASCII;
    } else if(strcmp(argv[i],"-b")==0) {
      options_format = FORMAT_BINARY;
    }
  }

  if(options_splitfile > 0) {
    sprintf(outfile_name, "%s-%d", outfile_name_stem, splitfile_count++);
  } else {
    sprintf(outfile_name, "%s", outfile_name_stem);
  }

  if((infile=fopen64(argv[argc-1],"rb"))==NULL) {
    fprintf(stderr,"error: unable to open file for reading: %s\n", argv[argc-1]);
    exit(1);
  }

  if((outfile=fopen64(outfile_name,"wb"))==NULL) {
    fprintf(stderr,"error: unable to open file for writing: %s\n", outfile_name);
    exit(1);
  }

  for(j=0;!feof(infile);j++) {

    result = fread(raw_records, sizeof(spadRecord), 1024, infile);
    if(result<1024) break;

    result = fread(&raw_stop, sizeof(spadStop), 1, infile);
    if(result<1) break;

    if(j > 0 && options_splitfile > 0 && j%options_splitfile ==0) {
      fclose(outfile);
      sprintf(outfile_name, "%s-%d", outfile_name_stem, splitfile_count++);
      if((outfile=fopen64(outfile_name,"wb"))==NULL) {
        fprintf(stderr,"error: unable to open file for writing: %s\n", outfile_name);
        exit(1);
      }
    }
    
    for(i=0;i<1024;i++) {
      if(raw_records[i].bits.coarse == 0b00111111) {
        current_frame.t[pixel_indexes[i]] = 65535;
      } else {
        current_frame.t[pixel_indexes[i]] = (raw_records[i].bits.coarse<<4 | raw_stop.bits.stop) - raw_records[i].bits.fine;
        // if previous was "negative", the count is invalid; we change it to 65535 (no data)
        if(current_frame.t[pixel_indexes[i]] > 32767) {
          current_frame.t[pixel_indexes[i]] = 65535;
        }
      }
    }

    for(i=0;i<1024;i++) {
      if(current_frame.t[i]!=65535) {
        if(options_format==FORMAT_ASCII) {
          fprintf(outfile, "%ld %d %d\n", j, i, current_frame.t[i]);
        } else if(options_format==FORMAT_BINARY) {
          outrec = ((uint64_t)j<<32) | ((uint64_t)i<<16) | ((uint64_t)current_frame.t[i]);
          fwrite(&outrec, 8, 1, outfile);
        }
      }
    }

    if(j%4096==0) {
      printf("Reading frame %ld ...\r", j);
      fflush(stdout);
    }
  }
  printf("done                              \n");

  fclose(infile);
  fclose(outfile);

  exit(0);
  return(0);
}
