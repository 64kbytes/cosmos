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

#define LINE_WIDTH 		80
#define MAX_FILE_COUNT 	50
#define OBS_DATA_EXT 	".rod"
#define OUT_FILE_EXT 	".fil"

// .obs file labels
#define SOURCE_NAME_STR 		"Source Name"
#define SOURCE_RA_STR 			"Source RA"
#define SOURCE_DEC_STR 			"Source DEC"
#define REF_DM_STR 				"Reference DM"
#define PERIOD_STR 				"Pulsar Period"
#define HI_OBS_FREQ_STR 		"Highest Observation Frequency (MHz)"
#define CHAN_OFFSET_FREQ_STR 	"Channel Offset (MHz)"
#define OBS_SAMPLING_PERIOD_STR "Observation Sampling Period (uS)" 
#define TELESCOPE_ID_STR 		"Telescope ID"
#define MACHINE_ID_STR 			"Machine ID"
#define DATA_TYPE_STR 			"Data Type"

#define REQ_COMMAND_LINE_PARAMS_COUNT 	3
#define REQ_OBS_DATA_PARAMS_COUNT 		11
#define DEFAULT_SAMPLE_PERIOD 			0.5

double mod_julian_date, sample_period, observation_freq_1, offset_freq, ref_dm, period;
double src_ra, src_dec, az_start, za_start;

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

float downsampling_ratio 		= 1; // overrided by command line arguments
char in_file_ext[NAME_MAX] 		= ".bin";
char out_file_name[NAME_MAX] 	= "out";
char in_file_path[NAME_MAX] 	= "in/";
char out_file_path[NAME_MAX] 	= "out/";

char obs_name[NAME_MAX], obs_filename[NAME_MAX], data_source_name[LINE_WIDTH], err_msg[LINE_WIDTH], obs_data_line[LINE_WIDTH];

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

void print_usage(){
	printf("\nUsage: multisdr2fil [-OPTION {value}] {observation_file} {modified_julian_date}\n\n");
	printf("OPTIONS\n");
	printf("-d:\tdownsamplig ratio\n");
	printf("-i:\tin file path\n");
	printf("-o:\tout file path\n");
	printf("-e:\tin file extension\n");
	printf("-f:\tout filename\n\n");
}

void print_settings(){
	printf("MJD:\t\t\t\t%f\n", mod_julian_date);
	printf("Downsampling ratio:\t\t%f\n", downsampling_ratio);
	printf("Input files folder path:\t%s\n", in_file_path);
	printf("Input files extension:\t\t%s\n", out_file_name);
	printf("Output file folder path:\t%s\n", out_file_path);
	printf("Output file name:\t\t%s\n", out_file_name);
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
				print_usage();
				return false;
				break;
			case 'd':
				downsampling_ratio = atof(optarg);
				break;
			case 'i':
				strcpy(in_file_path, optarg);
				break;
			case 'o':
				strcpy(out_file_path, optarg);
				break;
			case 'e':
				strcpy(in_file_ext, optarg);
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

				print_usage();
				return false;

			default:
				abort("Cannot parse provided arguments");
		}

	//strcpy(obs_filename, optarg);

	if(!argv[optind]){
		fprintf(stderr, "Missing observation file.\n");
		print_usage();
		return false;
	} 
	else {
		strcpy(obs_filename, argv[optind]);
		strip_path_and_extension(argv[optind], obs_name);
	}

	if(!argv[optind+1]){
		fprintf(stderr, "Missing MJD.\n");
		print_usage();
		return false;
	} 
	else {
		mod_julian_date = atof(argv[optind+1]);
	}

	//for (index = optind; index < argc; index++)
	//	printf ("Non-option argument %s\n", argv[index]);

	return true;
}

bool strip_path_and_extension(const char* filename, char* ret){
	char* name;
    char* last_dot;
    char* last_slash;

    // remove extension
	if((name = (char*) malloc(strlen(filename) + 1)) == NULL)
		return false;

	strcpy(name, filename);

	if((last_dot = strrchr(name, '.')) != NULL)
		*last_dot = '\0';

	// remove path
	if((last_slash = strrchr(name, '/')) != NULL ){
		*last_slash++;
		strcpy(name, last_slash);
	}

	strcpy(ret, name);

	free(name);
	return true;
}

void write_string(const char *string, FILE *out_str){
	int len = strlen(string);
	fwrite(&len, sizeof(int), 1, out_str);
	fwrite(string, sizeof(char), len, out_str);
}

void write_int(const char *name, int integer, FILE *out_int){
	write_string(name,out_int);
	fwrite(&integer,sizeof(int),1,out_int);
}

void write_double(const char *name, double double_precision, FILE *out_double){
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

void get_source_name_str(char *data_line)
{
	char *start_param_val = strstr(data_line,",");
	start_param_val++;
	char *end_param_val = NULL;
	end_param_val = strpbrk(data_line,"\n\r");
	
	if(end_param_val != NULL) 
		*end_param_val = '\0';
	
	strcpy(data_source_name, start_param_val);
}

int get_int_param_val(char *data_line)
{
	char *start_param_val = strstr(data_line,",");
	start_param_val++;
	char *end_param_val = NULL;
	end_param_val = strpbrk(data_line,"\n\r");
	if (end_param_val != NULL) *end_param_val = '\0';
	return atoi(start_param_val);
}

double get_double_param_val(char *data_line)
{
	char *start_param_val = strstr(data_line,",");
	start_param_val++;
	char *end_param_val = NULL;
	end_param_val = strpbrk(data_line,"\n\r");
	if (end_param_val != NULL) *end_param_val = '\0';
	return atof(start_param_val);
}

bool read_observation_data(const char* obs_filename){
	
	if(!(obs_file_ptr = fopen(obs_filename, "r"))){ 
		sprintf(err_msg, "Observation data file '%s' not found !\n", obs_filename);  
		return false; 
	};

	int line_ctrl = 0, param_count = 0;
	
	// parse .obs file
	while(fgets(obs_data_line, LINE_WIDTH, obs_file_ptr) != NULL){

		line_ctrl++;

		if(strstr(obs_data_line, SOURCE_NAME_STR))
			get_source_name_str(obs_data_line); 
		else
		if(strstr(obs_data_line, SOURCE_RA_STR))
			src_ra = get_double_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, SOURCE_DEC_STR))
			src_dec = get_double_param_val(obs_data_line); 
		else
		if(strstr(obs_data_line, REF_DM_STR))
			ref_dm = get_double_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, PERIOD_STR))
			period = get_double_param_val(obs_data_line); 
		else
		if(strstr(obs_data_line, HI_OBS_FREQ_STR))
			observation_freq_1 = get_double_param_val(obs_data_line); 
		else
		if(strstr(obs_data_line, CHAN_OFFSET_FREQ_STR))
			offset_freq = get_double_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, OBS_SAMPLING_PERIOD_STR))
			sample_period = get_double_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, TELESCOPE_ID_STR))
			telescope_id = get_int_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, MACHINE_ID_STR))
			machine_id = get_int_param_val(obs_data_line);
		else
		if(strstr(obs_data_line, DATA_TYPE_STR))
			data_type = get_int_param_val(obs_data_line);
		else {
			printf("Unrecognised data line (line %d)\n\n",line_ctrl); 
			break;
		}

		param_count++; 
	}

	fclose(obs_file_ptr);

	if(param_count < REQ_OBS_DATA_PARAMS_COUNT) {
		printf("Missing parameters in data file\n\n"); 
		return false; 
	}

	if(strcmp(obs_name, data_source_name) !=0){
		printf("Mismatched 'Source Name' in data file\n\n"); 
		return false; 
	}
	
	return true;
}

bool write_header(){

	char file_name_meta[LINE_WIDTH];

	char out_file[NAME_MAX] = "";

	strcat(out_file, out_file_path);
	strcat(out_file, out_file_name);
	strcat(out_file, OUT_FILE_EXT);

	/*
	char work_filename_1[LINE_WIDTH];
	char work_filename_2[LINE_WIDTH];

	strcpy(work_filename_1, in_filename_1);
	char *dotPosn = strpbrk(work_filename_1, ".");
	
	if(dotPosn != NULL){ 
		*dotPosn = '\0'; 
	}
	
	strcat(work_filename_1, OUTPUT_FILE_EXT);
	strcpy(out_file_name, work_filename_1);
	*/

	sample_period /= (double) 1000000.0; // Header expects seconds !!!

	if (downsampling_ratio !=1){
		sprintf(file_name_meta, "ds%f_%s", downsampling_ratio, out_file_name); 
		sample_period *= downsampling_ratio;
		strcpy(out_file_name, file_name_meta);
	}

	if(!(out_file_ptr = fopen(out_file, "wb"))){ 
		sprintf(err_msg, "Cannot open output file '%s'\n", out_file); 
		return false; 
	};

	write_string("HEADER_START", out_file_ptr);
	write_string("rawdatafile", out_file_ptr);
	write_string(in_file_path, out_file_ptr);

	if (!strcmp(data_source_name, "")) { 
		write_string("source_name", out_file_ptr); 
		write_string(data_source_name, out_file_ptr); 
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
	write_double("mod_julian_date", mod_julian_date, out_file_ptr);
	write_double("sample_period", sample_period, out_file_ptr);
	write_int("nifs",1, out_file_ptr);
	write_string("HEADER_END", out_file_ptr);

	fclose(out_file_ptr);

	return true;
	
}

bool check_header_data(){

	/*
	if(strlen(in_filename_1) < 4) {
		printf("'Raw Data File' is invalid!\n\n"); 
		return false;
	}
	*/

	if(strlen(obs_name) < 4) {
		printf("'Source Name' is invalid!\n\n"); 
		return false; 
	}

	if(src_ra == 0) { 
		printf("'Source RA' is invalid!\n\n"); 
		return false; 
	}

	if(src_dec == 0) { 
		printf("'Source DEC' is invalid!\n\n"); 
		return false; 
	}

	if(mod_julian_date == 0) {
		printf("'Start MJD' is invalid!\n\n"); 
		return false; 
	}

	if(sample_period == 0) { 
		printf("'Sample Period' is invalid!\n\n"); 
		return false; 
	}

	if(observation_freq_1 == 0) { 
		printf("'High Observation Freq.' is invalid!\n\n"); 
		return false; 
	}

	if(offset_freq == 0) { 
		printf("'Channel Offset Frequency' is invalid!\n\n"); 
		return false; 
	}

	if(ref_dm == 0) { 
		printf("'Reference DM' is invalid!\n\n"); 
		return false; 
	}

	if(period == 0) {
		printf("Period' is invalid!\n\n"); 
		return false; 
	}

	return true;
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

	if(!(read_observation_data(obs_filename)))
		abort("Cannot read observation file");

	if(!(check_header_data()))
		abort("Invalid header data");

	if(!(write_header()))
		abort("Cannot write header");

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