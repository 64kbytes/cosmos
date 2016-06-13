#ifdef _WIN32
#    ifdef __MINGW32__
#        define fseeko fseeko64
#      define ftello ftello64
#    else
#        define fseeko _fseeki64
#        define ftello _ftelli64
#    endif
#endif

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include "multisdr2fil.h"

#define MAX_FILE_COUNT 	50
#define OUT_FILE_EXT 	".fil"

struct BINFile{
	FILE* ptr;
	int length;
	unsigned long imt;
	unsigned long iout_max;
};

DIR* dir;
BINFile in_files[MAX_FILE_COUNT];
FILE* out_file_ptr, *obs_file_ptr;

int nbits = 32, machine_id, telescope_id, data_type, in_file_count;

float downsampling_ratio 		= 1; // can be overrided by params
char in_file_ext[NAME_MAX] 		= ".bin";
char out_file_name[NAME_MAX] 	= "out";
char in_file_path[NAME_MAX] 	= "in/";
char out_file_path[NAME_MAX] 	= "out/"; 

int open_input_files(const char* dir_name, BINFile* in_files){

	struct dirent *file;
	int i = 0, j = 0;
	char file_path[NAME_MAX];
	
	if((dir = opendir(dir_name)) != NULL){

		// print all the files and directories within directory
		while((file = readdir (dir)) != NULL){
					
			if(file->d_type == DT_DIR)
				continue;

			// check extension
			char* ext = strrchr(file->d_name, '.');
			if(strcmp(ext, in_file_ext) > 0){
				// ignore files with other extensions
				printf("Ignoring file: '%s'\n", file->d_name);
				continue;
			}

			// build file path
			strcpy(file_path, dir_name); 
			strcat(file_path, file->d_name);

			printf("Opening file: '%s'\n", file_path);

			BINFile bin_file;

			if(!(bin_file.ptr = fopen(file_path, "rb"))){
				
				printf("Error trying to open file %s\n", file_path);
				perror("");

				cleanup();				
				exit(1);
			}

			in_files[i] = bin_file;
			
			i++;
		}

		closedir(dir);
	
	} else {
		// could not open directory
		perror("");
		return EXIT_FAILURE;
	}

	printf("\n");

	return i;
}

void close_input_files(BINFile* files){
	int i, c = 0;

	for(i = 0; i < MAX_FILE_COUNT; i++)
		if(files[i].ptr){
			fclose(files[i].ptr);
			c++;
		}

	printf("Closed %u file/s\n", c);
}

long long get_file_length(FILE* file_ptr){
	
	long long eof;

	fseeko(file_ptr, 0L, SEEK_END);
	eof = ftello(file_ptr);
	eof = (eof >> 1) << 1; // make even to ensure complete I/Q pair
	fseeko(file_ptr, 0L, SEEK_SET);

	return eof;
}

long long get_min_data_length(int file_count, BINFile* in_files){
	// compares file length and return the minimum
	int i;
	long long min_data_length;

	for(i = 0; i < file_count; i++){
		
		if(!in_files[i].ptr)
			continue;

		in_files[i].length = get_file_length(in_files[i].ptr);

		// find smallest data length
		if(i == 0)
			min_data_length = in_files[i].length;
		else
		if(in_files[i].length < min_data_length)
			min_data_length = in_files[i].length;
	}

	return min_data_length;
}

unsigned long get_imt(unsigned char p0, unsigned char p1){
	return (p0 * p0) + (p1 * p1);
}

bool find_max_power(long long min_data_length, int downsampling_ratio, int file_count, BINFile* in_files){
	
	int i,j;
	unsigned char unsigned_char;
	signed char signed_char[2];

	unsigned long iout;
	unsigned long imt = 0; 
	int aux;
	int aux2 = 0;

	for(i = 0; i < file_count; i++){

		printf("Finding max power in dataset %u... ", i);

		for(aux = 0; aux < min_data_length-1; aux += 2)
		{
			// get pairs of unsigned char
			for(j=0; j < 2; j++){
				unsigned_char 	= getc(in_files[i].ptr);	
				signed_char[j] 	= (signed char)(unsigned_char-127);

				//printf("%u\n", unsigned_char);
				//printf("%d\n", in_files[i].signed_char[j]);
			}

			imt = imt + get_imt(signed_char[0], signed_char[1]);
			
			//imt = (sch1*sch1+sch2*sch2)+imt;
			
			aux2++;
			
			if(aux2 == downsampling_ratio){

				aux2 = 0;
				iout =  (unsigned long)(imt / downsampling_ratio);
				
				if (iout > in_files[i].iout_max) 
					in_files[i].iout_max = iout;
				
				imt = 0;
			}
		
		}

		// set file pointer back to the beginning
		rewind(in_files[i].ptr);

		printf("%lu\n", in_files[i].iout_max);

	}
	printf("\n");
	return true;
}

bool write_FIL_file(long long min_data_length, int downsampling_ratio, int file_count, BINFile* in_files){

	int i,j;
	unsigned char unsigned_char;
	signed char signed_char[2];
	char out_file[NAME_MAX] = "";

	strcat(out_file, out_file_path);
	strcat(out_file, out_file_name);
	strcat(out_file, OUT_FILE_EXT);

	//printf("%s\n", out_file);

	if(!(out_file_ptr = fopen(out_file, "wb"))){
		printf("Cannot create file %s\n", out_file);
		perror("");
		abort("");
	}

	long long aux;
	unsigned long imt1 = 0, imt2 = 0; 
	unsigned char ucha;
	signed char sch1;
	signed char sch2;
	unsigned long iout;
	float fout;

	int aux2 = 0;

	if(downsampling_ratio != 1) 
		printf("Downsampling by a factor of %d\n", downsampling_ratio);

	printf("Converting to 32-bit float data...\n\n");

	// for each datum
	for(aux = 0; aux < min_data_length-1; aux += 2)
	{
		//for each input file
		for(i = 0; i < file_count; i++){

			// get pairs of unsigned char
			for(j=0; j < 2; j++){
				unsigned_char 	= getc(in_files[i].ptr);	
				signed_char[j] 	= (signed char)(unsigned_char-127);
			}
			
			// calculate imt
			in_files[i].imt = in_files[i].imt + get_imt(signed_char[0], signed_char[1]);
		}

		aux2++;
		if(aux2 >= downsampling_ratio){

			//for each input file
			for(i = 0; i < file_count; i++){

				// write to output file
				iout = (unsigned long)(in_files[i].imt / downsampling_ratio);
				fout = (float)((iout * 250.0) / in_files[i].iout_max);
				fwrite( &fout, 1, sizeof(float), out_file_ptr ) ;

			}

			aux2 = 0;
			imt1 = 0;
			imt2 = 0;
		}
	}
	
	fclose(out_file_ptr);

	printf("Completed\n\n");
	return true;
}


void abort(const char* message){
	cleanup();
	printf("%s\n", message);
	exit(1);
}

void cleanup(){
	close_input_files(in_files);
}

void print_help(){
	printf("Help\n\n");
}

void print_settings(){
	printf("Input files folder path:\t%s\n", in_file_path);
	printf("Input files extension:\t\t%s\n", out_file_name);
	printf("Output file folder path:\t%s\n", out_file_path);
	printf("Output file name:\t\t%s\n", out_file_name);
	printf("Downsampling ratio:\t\t%f\n", downsampling_ratio);
	printf("Maximum input files allowed:\t%u\n\n", MAX_FILE_COUNT);
}

bool read_params(int argc, char* argv[]){
		
	int index;
	int c;

	opterr = 0;
	while((c = getopt(argc, argv, "hd:i:o:f:")) != -1)
		switch(c){
			
			//case 'a':
			//	aflag = 1;
			//	break;
			case 'h':
				print_help();
				return false;
				break;
			case 'd':
				downsampling_ratio = atof(optarg);
				break;
			case 'i':
				strcpy(in_file_path, optarg);
				break;
			case 'e':
				strcpy(in_file_ext, optarg);
				break;	 
			case 'o':
				strcpy(out_file_path, optarg);
				break;	  
			case 'f':
				strcpy(out_file_name, optarg);
				break; 
			case '?':
				if(optopt == 'd')
					fprintf(stderr, "Downsampling ratio (-%c): missing input files path.\n", optopt);
				else if(optopt == 'i')
					fprintf(stderr, "Input files folder path (-%c): missing input files path.\n", optopt);
				else if(optopt == 'o')
					fprintf(stderr, "Output files folder path (-%c): missing output files path.\n", optopt);
				else if(optopt == 'f')
					fprintf(stderr, "Output filename (-%c): missing filename.\n", optopt);
				else if(isprint(optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				return false;

			default:
				abort("Cannot parse provided arguments");
		}

	//for (index = optind; index < argc; index++)
	//	printf ("Non-option argument %s\n", argv[index]);

	return true;
}

void write_string(char *string, FILE *out_str){
	int len = strlen(string);
	fwrite(&len, sizeof(int), 1, out_str);
	fwrite(string, sizeof(char), len, out_str);
}

void write_int(char *name, int integer, FILE *out_int){
	write_string(name,out_int);
	fwrite(&integer,sizeof(int),1,out_int);

}

void write_double(char *name, double double_precision, FILE *out_double){
	write_string(name, out_double);
	fwrite(&double_precision,sizeof(double), 1, out_double);

}

void write_coords(double raj, double dej, double az, double za, FILE *out_coords){
	if ((raj != 0.0) || (raj != -1.0)) 
		write_double("src_ra", raj, out_coords);
	if ((dej != 0.0) || (dej != -1.0)) 
		write_double("src_dec", dej, out_coords);
	if ((az != 0.0)  || (az != -1.0))
		write_double("az_start", az, out_coords);
	if ((za != 0.0)  || (za != -1.0))
		write_double("za_start", za, out_coords);
}

bool write_header(){
	/*
	char work_filename_1[LINE_WIDTH];
	char work_filename_2[LINE_WIDTH];

	strcpy(work_filename_1, in_filename_1);
	char *dotPosn = strpbrk(work_filename_1, ".");
	
	if(dotPosn != NULL){ 
		*dotPosn = '\0'; 
	}
	
	strcat(work_filename_1, OUTPUT_FILE_EXT);
	strcpy(out_filename, work_filename_1);

	sample_period /= (double) 1000000.0; // Header expects seconds !!!

	if (downsampling_ratio !=1){

		sprintf(work_filename_2,"ds%d_%s",downsampling_ratio,work_filename_1); 
		sample_period *= downsampling_ratio;
		strcpy(out_filename, work_filename_2);
	}

	if(!(out_file_ptr = fopen(out_filename, "wb"))) { 

		sprintf(err_msg, "Cannot open output file '%s' !!!\n\n",out_filename); 
		printf(err_msg); return false; 
	};
	write_string("HEADER_START",out_file_ptr);
	write_string("rawdatafile",out_file_ptr);
	write_string(in_filename_1,out_file_ptr);

	if (!strcmp(source_name,"")) { 
		write_string("source_name",out_file_ptr); 
		write_string(source_name,out_file_ptr); 
	}
	
	write_int("machine_id", machine_id,out_file_ptr);
	write_int("telescope_id", telescope_id,out_file_ptr);
	write_coords(src_ra, src_dec, az_start, za_start, out_file_ptr);
	write_int("data_type",1, out_file_ptr);
	write_double("observation_freq_1", observation_freq_1, out_file_ptr);
	write_double("offset_freq", offset_freq, out_file_ptr);
	write_int("nchans",2, out_file_ptr);
	write_int("nbeams",1, out_file_ptr);
	write_int("ibeam",1, out_file_ptr);
	write_int("nbits", nbits, out_file_ptr);
	write_double("start_mjd", start_mjd, out_file_ptr);
	write_double("sample_period", sample_period, out_file_ptr);
	write_int("nifs",1, out_file_ptr);
	write_string("HEADER_END", out_file_ptr);

	fclose(out_file_ptr);

	return true;
	*/
}

int main(int argc, char* argv[]){

	if(!read_params(argc, argv))
		exit(1);

	print_settings();

	int in_file_count; 
	long long min_data_length;

	in_file_count = open_input_files(in_file_path, in_files);

	if(!(in_file_count > 0))
		abort("No files to process");

	printf("Processing %u %s file/s\n", in_file_count, in_file_ext);


	if(!(min_data_length = get_min_data_length(in_file_count, in_files)))
		abort("Cannot get min data length");
	
	if(!find_max_power(min_data_length, downsampling_ratio, in_file_count, in_files))
		abort("Cannot find max power");

	if(!write_FIL_file(min_data_length, downsampling_ratio, in_file_count, in_files))
		abort("Cannot write FIL file");

	cleanup();
	printf("Done\n\n");	
	exit(0);
}