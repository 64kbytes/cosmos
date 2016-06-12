#ifdef _WIN32
#    ifdef __MINGW32__
#        define fseeko fseeko64
#      define ftello ftello64
#    else
#        define fseeko _fseeki64
#        define ftello _ftelli64
#    endif
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "multisdr2fil.h"

#define MAX_FILE_COUNT 	50
#define IN_FILE_EXT 	".bin"
#define OUT_FILE_EXT 	".fil"

DIR* dir;
FILE* in_files[MAX_FILE_COUNT];
FILE* out_file_ptr, *obs_file_ptr;

int open_input_files(const char* dir_name, FILE** files){

	struct dirent *file;
	int i = 0, j = 0;
	char file_path[NAME_MAX];
	
	if((dir = opendir(dir_name)) != NULL){

		/* print all the files and directories within directory */
		while((file = readdir (dir)) != NULL){
					
			if(file->d_type == DT_DIR)
				continue;

			// check extension
			char* ext = strrchr(file->d_name, '.');
			if(strcmp(ext, IN_FILE_EXT) > 0){
				// ignore files with other extensions
				printf("Ignoring file: '%s'\n", file->d_name);
				continue;
			}

			// build file path
			strcpy(file_path, dir_name); 
			strcat(file_path, file->d_name);

			printf("Opening file: '%s'\n", file_path);

			if(!(files[i] = fopen(file_path, "rb"))){
				
				printf("Error trying to open file %s\n", file_path);
				perror("");

				cleanup();				
				exit(1);
			}
			
			i++;
		}

		closedir(dir);
	
	} else {
		/* could not open directory */
		perror("");
		return EXIT_FAILURE;
	}
	return i;
}

void close_input_files(FILE** files){
	int i, c = 0;

	for(i = 0; i < MAX_FILE_COUNT; i++)
		if(files[i]){
			fclose(files[i]);
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

bool find_max_power(int file_count){
	
	int i;
	long long eof, data_length;
	unsigned long *iout_max = new unsigned long[file_count];

	unsigned long iout;
	unsigned long imt = 0; 
	unsigned char ucha;
	signed char sch1;
	signed char sch2;
	int aux;
	int aux2 = 0;

	

	// get data length
	for(i = 0; i < file_count; i++){
		
		if(!in_files[i])
			continue;

		eof = get_file_length(in_files[i]);

		// find smallest data length
		if(i == 0)
			data_length = eof;
		else
		if(eof < data_length)
			data_length = eof;

		//printf("%p\n", in_files[i]);
	}

	printf("%lli\n", data_length);

	/*
	for(aux = 0; aux < data_length-1; aux +=2)
	{
		ucha = getc(in_file_ptr_1);	
		sch1 = (signed char)(ucha-127);
		ucha = getc(in_file_ptr_1);
		sch2 = (signed char)(ucha-127);
		imt = (sch1*sch1+sch2*sch2)+imt;
		aux2++;
		if(aux2==downsampling_ratio)
		{
			aux2 = 0;
			iout =  (unsigned long)(imt/downsampling_ratio);
			if (iout>ioutMax_1) ioutMax_1 = iout;
			imt = 0;
		}
	}
	*/


	return false;
}

void abort(const char* message){
	cleanup();
	printf("%s\n", message);
	exit(1);
}

void cleanup(){
	close_input_files(in_files);
}

int main(){

	int in_file_count; 
	
	in_file_count = open_input_files("in/", in_files);

	if(!(in_file_count > 0))
		abort("No files to process");

	printf("Processing %u %s file/s\n", in_file_count, IN_FILE_EXT);

	if(!find_max_power(in_file_count))
		abort("Cannot find max power");

	cleanup();
	exit(0);
}