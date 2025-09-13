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

FILE* g_logger = NULL;
bool g_console_out = true;
bool convert = false;
bool find = false;
int g_interval = 10000;
int g_self_affinity = 0b1111111100000000;
const char* g_prolasso_cfg_part_file = "prolasso.ini";
const char* g_out_file = "config.ini";
const char* g_cfg_file = "affinityServiceConfig.ini";
const char* g_blk_file = NULL;
SYSTEMTIME g_system_time;

typedef struct {
	char name[260];
	DWORD affinity_mask;  // NULLABLE in read, set affinity checks NULL
} ProcessConfig;

void log_message(const char* format, ...);
void convert_cfg();
bool is_admin();
void set_affinity(DWORD pid, DWORD_PTR affinity_mask, const char process_name[MAX_PATH]);
DWORD parse_affinity_range(const char* range_str);
void cfg_from_prolasso(IN const char* file_path, OUT ProcessConfig** configs, OUT int* count);
void read_config(IN const char* file_path, OUT ProcessConfig** configs, OUT int* count);
void print_help();

int main(int argc, char* argv[]) {
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	setlocale(LC_ALL, ".UTF8");

	// prase args
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "/?") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help();
			return 0;
		} else if (strcmp(argv[i], "-affinity") == 0 && i + 1 < argc) {
			char* bin_str = argv[++i];
			if (strncmp(bin_str, "0b", 2) == 0) {
				g_self_affinity = (int)strtol(bin_str + 2, NULL, 2);
			}
		} else if (strcmp(argv[i], "-console") == 0 && i + 1 < argc) {
			g_console_out = (strcmp(argv[++i], "true") == 0);
		} else if (strcmp(argv[i], "-find") == 0 && i + 1 < argc) {
			find = (strcmp(argv[++i], "true") == 0);
		} else if (strcmp(argv[i], "-plfile") == 0 && i + 1 < argc) {
			g_prolasso_cfg_part_file = argv[++i];
		} else if (strcmp(argv[i], "-out") == 0 && i + 1 < argc) {
			g_out_file = argv[++i];
		} else if (strcmp(argv[i], "-blacklist") == 0 && i + 1 < argc) {
			g_blk_file = argv[++i];
		} else if (strcmp(argv[i], "-convert") == 0) {
			convert = true;
		} else if (strcmp(argv[i], "-interval") == 0 && i + 1 < argc) {
			g_interval = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-config") == 0 && i + 1 < argc) {
			g_cfg_file = argv[++i];
		}
	}

	// logger init
	CreateDirectoryA("logs", NULL);
	char log_file_name[260];
	GetLocalTime(&g_system_time);
	sprintf_s(log_file_name, sizeof(log_file_name), "logs\\%04d%02d%02d.log", g_system_time.wYear, g_system_time.wMonth, g_system_time.wDay);
	g_logger = _fsopen(log_file_name, "a", _SH_DENYNO);
	if (!g_logger) {
		log_message("can't open log file");
	} else
		log_message("time interval: %d", g_interval);

	if (convert) {
		convert_cfg();
		return 0;
	}

	if (!g_console_out) {
		HWND h_wnd = GetConsoleWindow();
		if (h_wnd != NULL) {
			ShowWindow(h_wnd, SW_HIDE);
		}
	}

	if (!is_admin()) {
		log_message("not as admin, may not able to set affinity for some process");
	}

	HANDLE h_current_proc = GetCurrentProcess();
	if (SetProcessAffinityMask(h_current_proc, g_self_affinity)) {
		log_message("self affinity set");
	} else {
		log_message("self affinity set failed");
	}

	ProcessConfig* configs = NULL;
	int config_count = 0;
	read_config(g_cfg_file, &configs, &config_count);

	ProcessConfig* blk = NULL;
	int blk_count = 0;
	if (g_blk_file) read_config(g_blk_file, &blk, &blk_count);

	while (true) {
		PROCESSENTRY32W pe;
		pe.dwSize = sizeof(PROCESSENTRY32W);

		HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (h_snap == INVALID_HANDLE_VALUE) {
			log_message("can't snapshot processes");
			Sleep(g_interval);
			continue;
		}

		if (Process32FirstW(h_snap, &pe)) {
			do {
				char proc_name[MAX_PATH];
				if (WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, proc_name, sizeof(proc_name), NULL, NULL) == 0) {
					log_message("WideCharToMultiByte failed");
					continue;
				}
				_strlwr_s(proc_name, sizeof(proc_name));
				for (int i = 0; i < config_count; ++i) {
					if (_stricmp(proc_name, configs[i].name) == 0) {
						set_affinity(pe.th32ProcessID, configs[i].affinity_mask, proc_name);
						break;
					}
				}
				if (find) {
					HANDLE h_proc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
					if (h_proc == NULL) continue;
					DWORD_PTR current_mask, system_mask;
					if (!GetProcessAffinityMask(h_proc, &current_mask, &system_mask)) {
						CloseHandle(h_proc);
						continue;
					}
					if (current_mask == system_mask) {
						GetLocalTime(&g_system_time);

						if (blk) {
							bool in_blacklist = false;
							for (int j = 0; j < blk_count; ++j) {
								if (_stricmp(proc_name, blk[j].name) == 0) {
									in_blacklist = true;
									break;
								}
							}
							if (in_blacklist)
								continue;
							else
								log_message("%02d %02d:%02d:%02d find PID %lu - %s: %llu", g_system_time.wDay, g_system_time.wHour, g_system_time.wMinute,
									    g_system_time.wSecond, (unsigned long)pe.th32ProcessID, proc_name, (unsigned long long)current_mask);
						} else
							log_message("%02d %02d:%02d:%02d find PID %lu - %s: %llu", g_system_time.wDay, g_system_time.wHour, g_system_time.wMinute,
								    g_system_time.wSecond, (unsigned long)pe.th32ProcessID, proc_name, (unsigned long long)current_mask);
					}
				}
			} while (Process32NextW(h_snap, &pe));
		}

		CloseHandle(h_snap);
		Sleep(g_interval);
	}
	return 0;
}

void log_message(const char* format, ...) {
	char buffer[8192];
	va_list args;
	va_start(args, format);
	vsprintf_s(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (g_logger) {
		fprintf_s(g_logger, "%s\n", buffer);
		fflush(g_logger);
	}
	if (g_console_out) {
		printf("%s\n", buffer);
	}
}

static void set_affinity(DWORD pid, DWORD_PTR affinity_mask, const char process_name[MAX_PATH]) {
	HANDLE h_proc = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (h_proc == NULL) return;

	DWORD_PTR current_mask, system_mask;
	if (!GetProcessAffinityMask(h_proc, &current_mask, &system_mask)) {
		CloseHandle(h_proc);
		return;
	}

	if (current_mask != affinity_mask && affinity_mask != NULL) {
		if (SetProcessAffinityMask(h_proc, affinity_mask)) {
			char log_msg[512];
			GetLocalTime(&g_system_time);
			sprintf_s(log_msg, sizeof(log_msg), "%02d %02d:%02d:%02d - PID %lu - %s: -> %llu", g_system_time.wDay, g_system_time.wHour, g_system_time.wMinute,
				  g_system_time.wSecond, (unsigned long)pid, process_name, (unsigned long long)affinity_mask);
			log_message(log_msg);
		}
	}

	CloseHandle(h_proc);
}

bool is_admin() {
	BOOL is_elevated = FALSE;
	HANDLE token = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elevation;
		DWORD size;
		if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
			is_elevated = elevation.TokenIsElevated;
		}
	}
	if (token) {
		CloseHandle(token);
	}
	return is_elevated != 0;
}

void read_config(IN const char* file_path, OUT ProcessConfig** configs, OUT int* count) {
	FILE* f_path;
	if (fopen_s(&f_path, file_path, "r") != 0) {
		log_message("cant read file %s, trying create", file_path);
		fopen_s(&f_path, file_path, "w");
		if (f_path) fclose(f_path);
		return;
	}

	char line[512];
	int capacity = 16;
	*configs = (ProcessConfig*)malloc(capacity * sizeof(ProcessConfig));
	*count = 0;

	while (fgets(line, sizeof(line), f_path)) {
		char* newline_pos = strchr(line, '\n');
		if (newline_pos) *newline_pos = '\0';
		char* carriage_return_pos = strchr(line, '\r');
		if (carriage_return_pos) *carriage_return_pos = '\0';

		if (strlen(line) == 0 || line[0] == '#') {
			continue;
		}

		char* context = NULL;
		char* name = strtok_s(line, ",", &context);
		char* affinity_str = strtok_s(NULL, ",", &context);

		if (name) {
			DWORD affinity = NULL;
			if (affinity_str) affinity = atoll(affinity_str);
			if (*count >= capacity) {
				capacity *= 2;
				*configs = (ProcessConfig*)realloc(*configs, capacity * sizeof(ProcessConfig));
			}
			strcpy_s((*configs)[*count].name, sizeof((*configs)[*count].name), name);
			(*configs)[*count].affinity_mask = affinity;
			(*count)++;
		}
	}

	fclose(f_path);
}

DWORD parse_affinity_range(const char* range_str) {
	DWORD mask = 0;
	char* token_buf = _strdup(range_str);
	if (!token_buf) return 0;

	char* context = NULL;
	char* token = strtok_s(token_buf, ",", &context);

	while (token) {
		char* dash_pos = strchr(token, '-');
		if (dash_pos) {
			*dash_pos = '\0';
			int start = atoi(token);
			int end = atoi(dash_pos + 1);
			for (int i = start; i <= end; ++i)
				if (i < 32) mask |= (1 << i);
		} else {
			int core = atoi(token);
			if (core < 32) mask |= (1 << core);
		}
		token = strtok_s(NULL, ",", &context);
	}

	free(token_buf);
	return mask;
}

void convert_cfg() {
	ProcessConfig* configs = NULL;
	int count = 0;
	cfg_from_prolasso(g_prolasso_cfg_part_file, &configs, &count);
	log_message("found %d process in %s", count, g_prolasso_cfg_part_file);
	if (configs == NULL) {
		log_message("can't convert, %s may not exist.", g_prolasso_cfg_part_file);
		return;
	}

	FILE* fp;
	if (fopen_s(&fp, g_out_file, "w") != 0) {
		log_message("can't create, %s may be a diretory?", g_prolasso_cfg_part_file);
		if (configs) free(configs);
		return;
	}

	for (int i = 0; i < count; ++i) {
		fprintf_s(fp, "%s,%d\n", configs[i].name, configs[i].affinity_mask);
	}

	fclose(fp);
	log_message("convert done.");

	if (configs) free(configs);
}

void cfg_from_prolasso(IN const char* file_path, OUT ProcessConfig** configs, OUT int* count) {
	FILE* fp;
	if (fopen_s(&fp, file_path, "rb") != 0) {
		log_message("can't read prolasso cfg part file %s", file_path);
		return;
	}

	_fseeki64(fp, 0, SEEK_END);
	long long file_size = _ftelli64(fp);
	_fseeki64(fp, 0, SEEK_SET);

	if (file_size <= 0) {
		fclose(fp);
		log_message("empty file or ftell failed: %s", file_path);
		return;
	}

	char* buffer = (char*)malloc(file_size + 1);
	if (buffer == NULL) {
		fclose(fp);
		log_message("malloc failed");
		return;
	}

	size_t read_bytes = fread(buffer, 1, file_size, fp);
	if (read_bytes != file_size) log_message("fread failed: expected %lld, got %zu", file_size, read_bytes);

	buffer[read_bytes] = '\0';
	fclose(fp);

	char* token_buf = _strdup(buffer);
	char* context;
	char* token = strtok_s(token_buf, ",", &context);

	int capacity = 16;
	*configs = (ProcessConfig*)malloc(capacity * sizeof(ProcessConfig));
	*count = 0;

	while (token != NULL) {
		if (*count >= capacity) {
			capacity *= 2;
			*configs = (ProcessConfig*)realloc(*configs, capacity * sizeof(ProcessConfig));
		}

		strcpy_s((*configs)[*count].name, sizeof((*configs)[*count].name), token);

		token = strtok_s(NULL, ",", &context);
		if (token == NULL) break;

		token = strtok_s(NULL, ",", &context);
		if (token == NULL) break;
		(*configs)[*count].affinity_mask = parse_affinity_range(token);

		(*count)++;
		token = strtok_s(NULL, ",", &context);
	}

	free(token_buf);
	free(buffer);
}

void print_help() {
	printf("Usage: affinityService [options]\n");
	printf("Options:\n");
	printf("  -affinity <binary>    affinity for itselt (eg. 0b1111_0000)\n");
	printf("  -console <true|false> use console ouput?\n");
	printf("  -find <true|false>    find process with unset or default affinity?\n");
	printf("  -plfile <file>        file contains single line string behind ProcessLasso's ini's DefaultAffinitiesEx= (eg. prolasso.ini)\n");
	printf("                        eg. ?steamwebhelper.exe,0,8-19,everything.exe,0,8-19\n");
	printf("  -out <file>           output for convert (default config.ini)\n");
	printf("  -blacklist <file>     blacklist for find (default not work)\n");
	printf("  -convert              convert ProcessLasso's ini's part to pattern that this would use\n");
	printf("  -interval <ms>        time interval for checking all process again (ms, default 10000)\n");
	printf("  -config <file>        config file(default affinityServiceConfig.ini)\n");
}
