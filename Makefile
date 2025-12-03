CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g
LDFLAGS = -pthread

# Определение целевой архитектуры для указания библиотек
UNAME_S := $(shell uname -s)

TARGET = distributed_shm_server
SOURCES = distributed_shm_server.c
HEADERS = distributed_shm.h

# Правила сборки
$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

# Альтернативная цель с отладочной информацией
debug: CFLAGS += -DDEBUG -g
debug: $(TARGET)

# Очистка
clean:
	rm -f $(TARGET)

# Установка
install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Запуск сервера
run: $(TARGET)
	./$(TARGET)

# Запуск сервера с указанным портом
run-port: $(TARGET)
	./$(TARGET) $(PORT)

# Информация о сборке
info:
	@echo "Сборка: $(CC) $(CFLAGS)"
	@echo "Цель: $(TARGET)"
	@echo "Исходные файлы: $(SOURCES)"
	@echo "Заголовочные файлы: $(HEADERS)"

.PHONY: clean install run run-port info
