#ifndef CHUNK_STREAM_H
#define CHUNK_STREAM_H

#include <Arduino.h>
#include <Stream.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * ChunkStream - Stream-Adapter zwischen Push-Upload und Pull-Library
 *
 * Zweck: Die ESP32-targz-Library liest synchron von einem Stream (Pull-Modell).
 * Der WebServer-Upload-Callback liefert Daten chunkweise (Push-Modell).
 * ChunkStream verbindet beide Modelle ueber einen Ring-Buffer mit FreeRTOS-Semaphoren.
 *
 * Architektur:
 * - WebServer-Task (Core 1): ruft feed() auf, schreibt Chunks in den Ring-Buffer
 * - targz-Task (Core 0): ruft readBytes()/read() auf, liest aus dem Ring-Buffer
 * - Synchronisation: _dataReady (feed -> read) und _spaceReady (read -> feed)
 */
class ChunkStream : public Stream {
public:
    ChunkStream(size_t bufferSize = 4096) : _bufSize(bufferSize) {
        _buffer = (uint8_t*)malloc(bufferSize);
        _readPos = 0;
        _writePos = 0;
        _available = 0;
        _eof = false;
        _dataReady = xSemaphoreCreateBinary();
        _spaceReady = xSemaphoreCreateBinary();
        xSemaphoreGive(_spaceReady); // Initial: Buffer ist leer = Platz vorhanden
    }

    ~ChunkStream() {
        if (_buffer) free(_buffer);
        if (_dataReady) vSemaphoreDelete(_dataReady);
        if (_spaceReady) vSemaphoreDelete(_spaceReady);
    }

    // --- Aufgerufen vom WebServer-Upload-Task (Core 1) ---

    // Daten in den Ring-Buffer schreiben. Blockiert bis genug Platz ist.
    void feed(const uint8_t* data, size_t len) {
        size_t written = 0;
        while (written < len) {
            // Warten bis Platz im Buffer
            while (_available >= _bufSize) {
                xSemaphoreTake(_spaceReady, pdMS_TO_TICKS(100));
            }
            // Schreibe so viel wie moeglich
            size_t canWrite = min(len - written, _bufSize - _available);
            for (size_t i = 0; i < canWrite; i++) {
                _buffer[_writePos] = data[written + i];
                _writePos = (_writePos + 1) % _bufSize;
            }
            _available += canWrite;
            written += canWrite;
            xSemaphoreGive(_dataReady); // Signalisiere: Daten verfuegbar
        }
    }

    void setEOF() {
        _eof = true;
        xSemaphoreGive(_dataReady); // Aufwecken falls Library auf Daten wartet
    }

    void reset() {
        _readPos = 0;
        _writePos = 0;
        _available = 0;
        _eof = false;
        // Semaphore zuruecksetzen
        xSemaphoreTake(_dataReady, 0);
        xSemaphoreGive(_spaceReady);
    }

    // --- Stream-Interface (aufgerufen von ESP32-targz Library-Task, Core 0) ---

    // available() gibt aktuelle Byte-Anzahl zurueck (>= 0 laut Arduino Stream-Interface).
    // Die ESP32-targz-Library hat eine eigene Timeout-Logik (targz_read_timeout = 10s)
    // mit vTaskDelay(1)-Polling bei available() == 0.
    int available() override {
        return (int)_available;
    }

    int read() override {
        if (_available == 0 && !_eof) {
            // Warten auf Daten (max 10s, passt zu targz_read_timeout)
            xSemaphoreTake(_dataReady, pdMS_TO_TICKS(10000));
        }
        if (_available == 0) return -1;
        uint8_t b = _buffer[_readPos];
        _readPos = (_readPos + 1) % _bufSize;
        _available--;
        if (_available < _bufSize / 2) {
            xSemaphoreGive(_spaceReady); // Signalisiere: Platz frei
        }
        return b;
    }

    size_t readBytes(char* buf, size_t len) override {
        size_t totalRead = 0;
        while (totalRead < len) {
            if (_available == 0) {
                if (_eof) break;
                xSemaphoreTake(_dataReady, pdMS_TO_TICKS(10000));
                if (_available == 0 && _eof) break;
                if (_available == 0) break; // Timeout
            }
            size_t canRead = min(len - totalRead, _available);
            for (size_t i = 0; i < canRead; i++) {
                buf[totalRead + i] = _buffer[_readPos];
                _readPos = (_readPos + 1) % _bufSize;
            }
            _available -= canRead;
            totalRead += canRead;
            if (_available < _bufSize / 2) {
                xSemaphoreGive(_spaceReady);
            }
        }
        return totalRead;
    }

    int peek() override {
        if (_available == 0) return -1;
        return _buffer[_readPos];
    }

    size_t write(uint8_t) override { return 0; } // Nicht benoetigt

    bool isEOF() const { return _eof && _available == 0; }

private:
    uint8_t* _buffer;
    size_t _bufSize;
    volatile size_t _readPos;
    volatile size_t _writePos;
    volatile size_t _available;
    volatile bool _eof;
    SemaphoreHandle_t _dataReady;
    SemaphoreHandle_t _spaceReady;
};

#endif // CHUNK_STREAM_H
