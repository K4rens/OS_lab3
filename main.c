#include <stdio.h>
#include <windows.h>
#include <stdbool.h>
#include <string.h>

#define BUFFER_SIZE 64
#define MEMORY_NAME L"PrimeNumberMemory"

typedef struct {
    int number;
    bool is_prime;
    bool should_exit;
} shared_data;

bool is_prime(int n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0)
            return false;
    }
    return true;
}

void child_process() {
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MEMORY_NAME); //полный доступ, наследование нет, имя
    if (hMapFile == NULL) {
        printf("Error: Cannot open file mapping (Error %lu)\n", GetLastError());
        return;
    }
            //создание отображения файла в адресное пространоство процессора
    shared_data* data = (shared_data*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_data)); //наш мап, все действия, старшее, младшее слово смещения, размер
    if (data == NULL) {
        printf("Error: Cannot map view of file (Error %lu)\n", GetLastError());
        CloseHandle(hMapFile);
        return;
    }

    FILE* file = fopen("composite_numbers.txt", "a");
    if (file == NULL) {
        printf("Error: Cannot open composite_numbers.txt\n");
        UnmapViewOfFile(data);
        CloseHandle(hMapFile);
        return;
    }

    printf("Child process started. Waiting for numbers...\n");
    
    while (1) {
        Sleep(100); 
        
        if (data->number != 0) { //есть число для обработки
            printf("Child: Received number %d\n", data->number);
            
            //проверка на отр
            if (data->number < 0) {
                printf("Child: Negative number detected - exiting\n");
                data->should_exit = true;
                break;
            }
            //на простоту
            data->is_prime = is_prime(data->number);
            
            if (!data->is_prime) {
                //записываем составное число в файл
                fprintf(file, "%d\n", data->number);
                fflush(file);
                printf("Child: Number %d is composite - written to file\n", data->number);
            } else {
                printf("Child: Number %d is prime - exiting\n", data->number);
                data->should_exit = true;
                break;
            }
            
            data->number = 0; 
        }
        
        if (data->should_exit) {
            printf("Child: Exit signal received\n");
            break;
        }
    }

    //освобождение ресов
    fclose(file); 
    UnmapViewOfFile(data); //отключение разд памяти
    CloseHandle(hMapFile); 
    printf("Child process finished\n");
}


void parent_process() {
    HANDLE hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                        0, sizeof(shared_data), MEMORY_NAME);        
    if (hMapFile == NULL) {
        printf("Error: Cannot create file mapping (Error %lu)\n", GetLastError());
        return;
    }

    shared_data* data = (shared_data*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_data));
    if (data == NULL) {
        printf("Error: Cannot map view of file (Error %lu)\n", GetLastError());
        CloseHandle(hMapFile);
        return;
    }

    //начальные значения 
    data->number = 0;
    data->is_prime = false;
    data->should_exit = false;

    STARTUPINFOW si = {0};   //все заполнено нулями параметры процесса
    PROCESS_INFORMATION pi = {0}; //инфа о процессе
    si.cb = sizeof(si); //для винды, размер всегда надо указывать

    wchar_t cmdline[MAX_PATH];
    GetModuleFileNameW(NULL, cmdline, MAX_PATH);  //путь к тек программе, куда записать рез, макс буффер
    
    wchar_t* lastSlash = wcsrchr(cmdline, L'\\'); //поиск посл "\"
    if (lastSlash) {
        wcscpy(lastSlash + 1, L"child.exe");
    } else {
        wcscpy(cmdline, L"child.exe");
    }
    
    printf("Creating child process...\n");
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { //имя, куда, безоп, безоп, наслед, окруж наследуется, тек каталог
        printf("Error: Cannot create child process (Error %lu)\n", GetLastError());
        UnmapViewOfFile(data);
        CloseHandle(hMapFile);
        return;
    }

    printf("Child process created successfully (PID: %lu)\n", pi.dwProcessId);
    printf("===========================================\n");
    printf("Enter numbers (one per line).\n");
    printf("Program will exit when you enter:\n");
    printf("  - A negative number\n");
    printf("  - A prime number\n");
    printf("===========================================\n\n");
    
    char input[BUFFER_SIZE];
    
    while (1) {
        printf("Enter number: ");
        fflush(stdout);
        
        if (fgets(input, BUFFER_SIZE, stdin) == NULL) {
            printf("Input error\n");
            break;
        }

        int num;
        if (sscanf(input, "%d", &num) != 1) {
            printf("Invalid input. Please enter a valid integer.\n");
            continue;
        }

        printf("Parent: Sending number %d to child...\n", num);
        
        //ждем пока дочерний процесс освободит буфер
        while (data->number != 0 && !data->should_exit) {
            Sleep(50);
        }

        if (data->should_exit) {
            printf("Parent: Child requested exit\n");
            break;
        }

        data->number = num; //передаем число дочернему процессу

        if (num < 0) {
            printf("Parent: Negative number entered - initiating exit\n");
            data->should_exit = true;
            break;
        }

        Sleep(200);
        
        if (data->is_prime) {
            printf("Parent: Prime number detected - initiating exit\n");
            data->should_exit = true;
            break;
        }
        
        printf("Parent: Waiting for next number...\n");
    }

    printf("Parent: Waiting for child process to finish...\n");
    WaitForSingleObject(pi.hProcess, 500);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    UnmapViewOfFile(data);
    CloseHandle(hMapFile);
    
    printf("\n===========================================\n");
    printf("Parent process finished\n");
    printf("Check 'composite_numbers.txt' for results\n");
    printf("===========================================\n");
}

int main() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    wchar_t* exeName = wcsrchr(exePath, L'\\');
    if (exeName == NULL) {
        exeName = exePath;
    } else {
        exeName++;
    }
    
    if (wcsicmp(exeName, L"child.exe") == 0) {
        child_process();
    } else {
        parent_process();
    }
    
    return 0;
}