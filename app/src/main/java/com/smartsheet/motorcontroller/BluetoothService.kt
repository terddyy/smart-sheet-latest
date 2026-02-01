package com.smartsheet.motorcontroller

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.Context
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.*

@SuppressLint("MissingPermission")
class BluetoothService(private val context: Context) {

    companion object {
        private const val ESP32_DEVICE_NAME = "SmartSheet_ESP32"
        private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }

    private val bluetoothManager: BluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter

    private var bluetoothSocket: BluetoothSocket? = null
    private var inputStream: InputStream? = null
    private var outputStream: OutputStream? = null

    private var readJob: Job? = null
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Connecting : ConnectionState()
        object Connected : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState

    private val _receivedData = MutableStateFlow<String>("")
    val receivedData: StateFlow<String> = _receivedData

    fun isBluetoothAvailable(): Boolean = bluetoothAdapter != null

    fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    suspend fun connect(): Boolean = withContext(Dispatchers.IO) {
        if (!isBluetoothAvailable() || !isBluetoothEnabled()) {
            _connectionState.value = ConnectionState.Error("Bluetooth not available or enabled")
            return@withContext false
        }

        _connectionState.value = ConnectionState.Connecting

        try {
            val device = findESP32Device()
            if (device == null) {
                _connectionState.value = ConnectionState.Error("ESP32 device not found. Please pair first.")
                return@withContext false
            }

            bluetoothAdapter?.cancelDiscovery()

            bluetoothSocket = device.createRfcommSocketToServiceRecord(SPP_UUID)
            bluetoothSocket?.connect()

            inputStream = bluetoothSocket?.inputStream
            outputStream = bluetoothSocket?.outputStream

            _connectionState.value = ConnectionState.Connected

            startReading()

            true
        } catch (e: IOException) {
            _connectionState.value = ConnectionState.Error("Connection failed: ${e.message}")
            disconnect()
            false
        }
    }

    private fun findESP32Device(): BluetoothDevice? {
        val pairedDevices = bluetoothAdapter?.bondedDevices
        return pairedDevices?.find { it.name == ESP32_DEVICE_NAME }
    }

    fun disconnect() {
        readJob?.cancel()

        try {
            inputStream?.close()
            outputStream?.close()
            bluetoothSocket?.close()
        } catch (e: IOException) {
        }

        inputStream = null
        outputStream = null
        bluetoothSocket = null

        _connectionState.value = ConnectionState.Disconnected
    }

    suspend fun sendCommand(command: String): Boolean = withContext(Dispatchers.IO) {
        if (_connectionState.value != ConnectionState.Connected) {
            return@withContext false
        }

        try {
            outputStream?.write("$command\n".toByteArray())
            outputStream?.flush()
            true
        } catch (e: IOException) {
            _connectionState.value = ConnectionState.Error("Send failed: ${e.message}")
            disconnect()
            false
        }
    }

    private fun startReading() {
        readJob = serviceScope.launch {
            val buffer = ByteArray(1024)
            val stringBuilder = StringBuilder()

            while (isActive && _connectionState.value == ConnectionState.Connected) {
                try {
                    val bytesRead = inputStream?.read(buffer) ?: -1
                    if (bytesRead > 0) {
                        val data = String(buffer, 0, bytesRead)
                        stringBuilder.append(data)

                        val lines = stringBuilder.toString().split("\n")
                        for (i in 0 until lines.size - 1) {
                            _receivedData.value = lines[i].trim()
                        }

                        stringBuilder.clear()
                        stringBuilder.append(lines.last())
                    }
                } catch (e: IOException) {
                    if (isActive) {
                        _connectionState.value = ConnectionState.Error("Read failed: ${e.message}")
                        disconnect()
                    }
                    break
                }
            }
        }
    }

    fun cleanup() {
        disconnect()
        serviceScope.cancel()
    }
}