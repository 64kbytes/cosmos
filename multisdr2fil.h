int open_input_files(const char* dir_name, FILE** files);
void close_input_files(FILE** files);
bool find_max_power(int file_count);
long long get_file_length(FILE* file_ptr);
void abort(const char* message);
void cleanup();