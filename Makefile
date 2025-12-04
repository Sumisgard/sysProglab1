CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g
LDFLAGS = -pthread

# Определение целевой архитектуры для указания библиотек
UNAME_S := $(shell uname -s)

SERVER_TARGET = distributed_shm_server
CLIENT_TARGET = libdistributed_shm.a
CLIENT_SHARED_TARGET = libdistributed_shm.so
TEST_TARGET = test_dshm
TEST_ERRORS_TARGET = test_dshm_errors
EXAMPLE_TARGET = example_usage

SERVER_SOURCES = distributed_shm_server.c
CLIENT_SOURCES = distributed_shm_client.c
TEST_SOURCES = test_dshm.c
TEST_ERRORS_SOURCES = test_dshm_errors.c
EXAMPLE_SOURCES = example_usage.c
HEADERS = distributed_shm.h distributed_shm_client.h

# Правила сборки
all: server client

server: $(SERVER_TARGET)

client: $(CLIENT_TARGET) $(CLIENT_SHARED_TARGET)

test: $(TEST_TARGET)

test-errors: $(TEST_ERRORS_TARGET)

example: $(EXAMPLE_TARGET)

$(SERVER_TARGET): $(SERVER_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_SOURCES) $(LDFLAGS)

# Статическая библиотека клиента
$(CLIENT_TARGET): $(CLIENT_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -c $(CLIENT_SOURCES)
	ar rcs $(CLIENT_TARGET) distributed_shm_client.o
	rm -f distributed_shm_client.o

# Динамическая библиотека клиента
$(CLIENT_SHARED_TARGET): $(CLIENT_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -c $(CLIENT_SOURCES)
	$(CC) -shared -o $(CLIENT_SHARED_TARGET) distributed_shm_client.o
	rm -f distributed_shm_client.o

# Тестовая программа
$(TEST_TARGET): $(TEST_SOURCES) $(CLIENT_TARGET)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_SOURCES) -L. -ldistributed_shm -pthread

# Тестовая программа для ошибок
$(TEST_ERRORS_TARGET): $(TEST_ERRORS_SOURCES) $(CLIENT_TARGET)
	$(CC) $(CFLAGS) -o $(TEST_ERRORS_TARGET) $(TEST_ERRORS_SOURCES) -L. -ldistributed_shm -pthread

# Пример программы
$(EXAMPLE_TARGET): $(EXAMPLE_SOURCES) $(CLIENT_TARGET)
	$(CC) $(CFLAGS) -o $(EXAMPLE_TARGET) $(EXAMPLE_SOURCES) -L. -ldistributed_shm -pthread

# Альтернативная цель с отладочной информацией
debug: CFLAGS += -DDEBUG -g
debug: all

# Очистка
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(CLIENT_SHARED_TARGET) $(TEST_TARGET) $(TEST_ERRORS_TARGET) $(EXAMPLE_TARGET) *.o

# Установка
install: server client
	install -m 755 $(SERVER_TARGET) /usr/local/bin/
	install -m 644 $(CLIENT_TARGET) /usr/local/lib/
	install -m 644 $(CLIENT_SHARED_TARGET) /usr/local/lib/
	install -m 644 distributed_shm.h /usr/local/include/
	install -m 644 distributed_shm_client.h /usr/local/include/

# Запуск сервера
run: server
	./$(SERVER_TARGET)

# Запуск сервера с указанным портом
run-port: server
	./$(SERVER_TARGET) $(PORT)

# Запуск теста (предполагается, что сервер запущен)
run-test: test
	./$(TEST_TARGET)

# Запуск теста ошибок (предполагается, что сервер запущен)
run-test-errors: test-errors
	./$(TEST_ERRORS_TARGET)

# Запуск примера (предполагается, что сервер запущен)
run-example: example
	./$(EXAMPLE_TARGET)

# Информация о сборке
info:
	@echo "Сборка: $(CC) $(CFLAGS)"
	@echo "Цель: $(SERVER_TARGET), $(CLIENT_TARGET), $(TEST_TARGET), $(TEST_ERRORS_TARGET) и $(EXAMPLE_TARGET)"
	@echo "Исходные файлы сервера: $(SERVER_SOURCES)"
	@echo "Исходные файлы клиента: $(CLIENT_SOURCES)"
	@echo "Тестовые файлы: $(TEST_SOURCES), $(TEST_ERRORS_SOURCES)"
	@echo "Пример файла: $(EXAMPLE_SOURCES)"
	@echo "Заголовочные файлы: $(HEADERS)"

.PHONY: all server client test test-errors example clean install run run-port run-test run-test-errors run-example info
