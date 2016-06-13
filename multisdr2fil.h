struct BINFile;

bool read_params(int argc, char* argv[]);
int open_input_files(const char* dir_name, BINFile* in_files);
void close_input_files(FILE** files);

long long get_file_length(FILE* file_ptr);
long long get_min_data_length(int file_count, BINFile* in_files);
unsigned long get_imt(unsigned char p0, unsigned char p1);

void write_string(char *string, FILE *out_str);
void write_int(char *name, int integer, FILE *out_int);
void write_double(char *name, double double_precision, FILE *out_double);

bool find_max_power(long long min_data_length, int downsampling_ratio, int file_count, BINFile* in_files);
bool write_header();
bool write_FIL_file(long long min_data_length, int downsampling_ratio, int file_count, BINFile* in_files);

void abort(const char* message);
void cleanup();