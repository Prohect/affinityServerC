#include <locale.h>
#include <minwindef.h>
#include <psapi.h>
#include <share.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <windows.h>

static FILE *g_logger = NULL;
static bool g_console_out = 0;
static bool g_convert_mode = 0;
static bool g_find_unset_affinity = 0;
static int g_interval = 10000;
static unsigned long long g_self_affinity = 0;
static const char *g_convert_in_file = "prolasso.ini";
static const char *g_convert_out_file = "output.ini";
static const char *g_cfg_file = "config.ini";
static const char *g_blk_file = NULL;
static SYSTEMTIME g_time;

typedef struct {
	char name[MAX_PATH];
	unsigned long long affinity_mask;
} ProcessConfig;

static int log_message(const char *format, ...);
static int log_message_flush(const char *format, ...);
static int convert_cfg();
static bool is_admin();
static void set_affinity(unsigned long pid, unsigned long long affinity_mask, const char process_name[MAX_PATH]);
static unsigned long long parse_affinity_range(const char *range_str);
static void cfg_from_prolasso(IN const char *file_path, OUT ProcessConfig **configs, OUT int *count);
static int read_config(IN const char *file_path, OUT ProcessConfig **configs, OUT int *count);
static int print_help();

int main(int argc, char *argv[]) {
	GetLocalTime(&g_time);
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	setlocale(LC_ALL, ".UTF8");
	for (int i = 1; i < argc; ++i) { // prase args
		if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "/?") == 0 || strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "?") == 0 || strcmp(argv[i], "--help") == 0) {
			return print_help();
		} else if (strcmp(argv[i], "-affinity") == 0 && i + 1 < argc) {
			char *bin_str = argv[++i];
			g_self_affinity = strtoull(bin_str, NULL, 0);
		} else if (strcmp(argv[i], "-console") == 0 && i + 1 < argc) {
			g_console_out = 1;
		} else if (strcmp(argv[i], "-find") == 0 && i + 1 < argc) {
			g_find_unset_affinity = 1;
		} else if (strcmp(argv[i], "-convert") == 0) {
			g_convert_mode = 1;
		} else if (strcmp(argv[i], "-plfile") == 0 && i + 1 < argc) {
			g_convert_in_file = argv[++i];
		} else if (strcmp(argv[i], "-out") == 0 && i + 1 < argc) {
			g_convert_out_file = argv[++i];
		} else if (strcmp(argv[i], "-blacklist") == 0 && i + 1 < argc) {
			g_blk_file = argv[++i];
		} else if (strcmp(argv[i], "-interval") == 0 && i + 1 < argc) {
			g_interval = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-config") == 0 && i + 1 < argc) {
			g_cfg_file = argv[++i];
		}
	}
	if (g_interval < 16) g_interval = 16;
	if (g_convert_mode) return convert_cfg(); // convert or service mode
	if (!g_console_out) {			  // logger's file output stream
		CreateDirectoryA("logs", NULL);
		char log_file_name[MAX_PATH];
		sprintf_s(log_file_name, sizeof(log_file_name), "logs\\%04d%02d%02d.log", g_time.wYear, g_time.wMonth, g_time.wDay);
		g_logger = _fsopen(log_file_name, "a", _SH_DENYNO);
		if (!g_logger)
			log_message("can't open log file");
		else {
			HWND h_wnd = GetConsoleWindow();
			if (h_wnd) ShowWindow(h_wnd, SW_HIDE);
		}
	}
	log_message("Affinity Service started");
	log_message("time interval: %d", g_interval);
	if (!is_admin()) log_message("not as admin, may not able to set affinity for some process");
	if (g_self_affinity) {
		if (SetProcessAffinityMask(GetCurrentProcess(), g_self_affinity))
			log_message("self affinity set %llu", g_self_affinity);
		else
			log_message("self affinity set failed: %llu", g_self_affinity);
	}
	ProcessConfig *configs, *blk = NULL; // load blacklist for find unset affinity process and config
	int config_count, blk_count = 0;
	if (!read_config(g_cfg_file, &configs, &config_count)) return log_message_flush("no valid config, exiting");
	if (g_blk_file) read_config(g_blk_file, &blk, &blk_count);
	HANDLE processes_snapshot; // init variables for loop
	PROCESSENTRY32W process;
	char proc_name[MAX_PATH];
	process.dwSize = sizeof(PROCESSENTRY32W);
	while (1) {
		processes_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (processes_snapshot != INVALID_HANDLE_VALUE) {
			if (Process32FirstW(processes_snapshot, &process)) {
				do {
					if (WideCharToMultiByte(CP_UTF8, 0, process.szExeFile, -1, proc_name, sizeof(proc_name), NULL, NULL) != 0) {
						_strlwr_s(proc_name, sizeof(proc_name));
						for (int i = 0; i < config_count; ++i)
							if (_stricmp(proc_name, configs[i].name) == 0) {
								set_affinity(process.th32ProcessID, configs[i].affinity_mask, proc_name);
								goto skip;
							}
						if (g_find_unset_affinity) {
							HANDLE h_proc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, process.th32ProcessID);
							if (h_proc) {
								unsigned long long current_mask, system_mask;
								if (GetProcessAffinityMask(h_proc, &current_mask, &system_mask)) {
									if (current_mask == system_mask) {
										if (blk_count)
											for (int j = 0; j < blk_count; ++j)
												if (_stricmp(proc_name, blk[j].name) == 0) goto skip_log;
										log_message("find PID %-5lu - %s: %llu", process.th32ProcessID, proc_name, current_mask);
									skip_log:;
									}
								}
								CloseHandle(h_proc);
							}
						}
					} else
						log_message("WideCharToMultiByte failed");
				skip:;
				} while (Process32NextW(processes_snapshot, &process));
			}
		} else
			log_message("can't snapshot processes");
		CloseHandle(processes_snapshot);
		if (g_logger) fflush(g_logger);
		Sleep(g_interval);
		GetLocalTime(&g_time);
	}
	return 0;
}

static int log_message(const char *format, ...) {
	static char buf[8192] = "";
	static char buffer[8192] = "";
	va_list args;
	va_start(args, format);
	vsprintf_s(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (!strcmp(buffer, buf) == 0) {
		strcpy_s(buf, sizeof(buf), buffer);
		if (g_logger)
			fprintf_s(g_logger, "[%02d %02d:%02d:%02d]%s\n", g_time.wDay, g_time.wHour, g_time.wMinute, g_time.wSecond, buffer);
		else
			printf("[%02d %02d:%02d:%02d]%s\n", g_time.wDay, g_time.wHour, g_time.wMinute, g_time.wSecond, buffer);
	}
	return 0;
}

static int log_message_flush(const char *format, ...) {
	log_message(format);
	if (g_logger) fflush(g_logger);
	return 0;
}

static void set_affinity(unsigned long pid, unsigned long long affinity_mask, const char process_name[MAX_PATH]) {
	if (affinity_mask) {
		HANDLE h_proc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
		unsigned long long current_mask, system_mask;
		if (h_proc) {
			if (GetProcessAffinityMask(h_proc, &current_mask, &system_mask) && current_mask != affinity_mask && SetProcessAffinityMask(h_proc, affinity_mask)) log_message("PID %-5lu - %s: -> %llu", pid, process_name, affinity_mask);
			CloseHandle(h_proc);
		}
	}
}

static bool is_admin() {
	bool is_elevated = 0;
	HANDLE token = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elevation;
		DWORD size;
		if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) is_elevated = elevation.TokenIsElevated;
		CloseHandle(token);
	}
	return is_elevated;
}

static int read_config(IN const char *file_path, OUT ProcessConfig **configs, OUT int *count) {
	FILE *f_path;
	if (fopen_s(&f_path, file_path, "r") == 0) {
		char line[8192];
		int capacity = 16;
		*configs = (ProcessConfig *)malloc(capacity * sizeof(ProcessConfig));
		*count = 0;
		while (fgets(line, sizeof(line), f_path)) {
			char *newline_pos = strchr(line, '\n');
			if (newline_pos) *newline_pos = '\0';
			char *carriage_return_pos = strchr(line, '\r');
			if (carriage_return_pos) *carriage_return_pos = '\0';
			if (strlen(line) == 0 || line[0] == '#') continue;
			char *context = NULL;
			char *name = strtok_s(line, ",", &context);
			char *affinity_str = strtok_s(NULL, ",", &context);
			if (name) {
				unsigned long long affinity = 0;
				if (affinity_str) affinity = strtoull(affinity_str, 0, 0);
				if (*count >= capacity) {
					capacity *= 2;
					ProcessConfig *temp = (ProcessConfig *)realloc(*configs, capacity * sizeof(ProcessConfig));
					if (!temp) break;
					*configs = temp;
				}
				strcpy_s((*configs)[*count].name, sizeof((*configs)[*count].name), name);
				(*configs)[(*count)++].affinity_mask = affinity;
			}
		}
		fclose(f_path);
		return *count;
	} else {
		log_message("cant read file %s, trying create", file_path);
		fopen_s(&f_path, file_path, "w");
		if (f_path) fclose(f_path);
		return 0;
	}
}

static unsigned long long parse_affinity_range(const char *range_str) {
	char *affinity_token_buf = _strdup(range_str);
	if (!affinity_token_buf) return 0;
	unsigned long long mask = 0;
	char *context = NULL;
	char *token = strtok_s(affinity_token_buf, ",", &context);
	while (token) {
		char *dash_pos = strchr(token, '-');
		if (dash_pos) {
			*dash_pos = '\0';
			int start = atoi(token);
			int end = atoi(dash_pos + 1);
			for (int i = start; i <= end; ++i)
				if (i < 32) mask |= (1ull << i);
		} else {
			int core = atoi(token);
			if (core < 32) mask |= (1ull << core);
		}
		token = strtok_s(NULL, ",", &context);
	}
	free(affinity_token_buf);
	return mask;
}

static int convert_cfg() {
	ProcessConfig *configs = NULL;
	int count = 0;
	cfg_from_prolasso(g_convert_in_file, &configs, &count);
	if (configs != NULL) {
		log_message("found %d process in %s", count, g_convert_in_file);
		FILE *convert_out_file_stream;
		if (fopen_s(&convert_out_file_stream, g_convert_out_file, "w") == 0) {
			for (int i = 0; i < count; ++i) fprintf_s(convert_out_file_stream, "%s,%llu\n", configs[i].name, configs[i].affinity_mask);
			fclose(convert_out_file_stream);
			log_message("convert done.");
		} else
			log_message("can't create, %s may be a diretory?", g_convert_out_file);
	} else
		log_message("can't convert, %s may not exist.", g_convert_in_file);
	return 0;
}

static void cfg_from_prolasso(IN const char *file_path, OUT ProcessConfig **configs, OUT int *count) {
	FILE *fp;
	if (fopen_s(&fp, file_path, "rb") == 0) {
		_fseeki64(fp, 0, SEEK_END);
		long long file_size = _ftelli64(fp);
		_fseeki64(fp, 0, SEEK_SET);
		if (file_size > 0) {
			char *buffer = (char *)malloc(file_size + 1);
			if (buffer) {
				size_t read_bytes = fread(buffer, 1, file_size, fp);
				buffer[read_bytes] = '\0';
				char *token_buf = _strdup(buffer);
				char *context = NULL;
				char *token = strtok_s(token_buf, ",", &context);
				int capacity = 16;
				*configs = (ProcessConfig *)malloc(capacity * sizeof(ProcessConfig));
				*count = 0;
				if (*configs) {
					while (token) {
						if (*count >= capacity) {
							capacity *= 2;
							ProcessConfig *temp = (ProcessConfig *)realloc(*configs, capacity * sizeof(ProcessConfig));
							if (!temp) break;
							*configs = temp;
						}
						memset(&(*configs)[*count], 0, sizeof(ProcessConfig));
						strncpy_s((*configs)[*count].name, sizeof((*configs)[*count].name), token, _TRUNCATE);
						token = strtok_s(NULL, ",", &context); // skip "socket of core"
						if (token == NULL) break;
						token = strtok_s(NULL, ",", &context); // core range
						if (token == NULL) break;
						(*configs)[(*count)++].affinity_mask = parse_affinity_range(token);
						token = strtok_s(NULL, ",", &context);
					}
				}
				free(token_buf);
				free(buffer);
			} else {
				log_message("malloc failed");
			}
		} else {
			log_message("empty file or ftell failed: %s", file_path);
		}
		fclose(fp);
	} else {
		log_message("can't read prolasso cfg part file %s", file_path);
	}
}

static int print_help() {
	printf("Usage: affinityService [options]\n");
	printf("Options:\n");
	printf("  -affinity <integer>   affinity for itself (eg. 0b11110000 or 0xFFFF or 254)\n");
	printf("  -find                 find process with unset or default affinity?\n");
	printf("  -console              use console ouput?\n");
	printf("  -config <file>        config file(default config.ini)\n");
	printf("  -interval <ms>        time interval for checking all process again (ms, default 10000)\n");
	printf("  -convert              convert ProcessLasso's ini's part to pattern that this would use\n");
	printf("  -blacklist <file>     blacklist for find (default not work)\n");
	printf("  -plfile <file>        UTF8 file contains single line string from behind prolasso's DefaultAffinitiesEx=\n");
	printf("                        eg. steamwebhelper.exe,0,8-19,everything.exe,0,8-19\n");
	printf("  -out <file>           output for convert (default output.ini)\n");
	return 0;
}
