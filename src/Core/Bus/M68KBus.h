#pragma once

#include <cstdint>
#include <stdexcept>

namespace R2MEGA::Core
{
    // Resposta do dispositivo ao reconhecimento de uma interrupção.
    struct M68KInterruptResponse
    {
        enum class Kind
        {
            Autovector,
            DeviceVector,
            Spurious
        };
        Kind kind = Kind::Autovector;
        std::uint8_t vector = 0;
    };

    // O dispositivo sinaliza uma falha definitiva; false em tryRead/tryWrite indica espera.
    class M68KBusError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Amostra por ciclo de CPU: cada fase agrupa dois estados do barramento (S0-S7).
    struct M68KPins
    {
        std::uint32_t address = 0;
        std::uint16_t data = 0;
        unsigned phase = 0;
        unsigned functionCode = 0;
        bool active = false;
        bool addressStrobe = false;
        bool upperDataStrobe = false;
        bool lowerDataStrobe = false;
        bool waiting = false;
        bool dataValid = false;
        bool write = false;
        bool readModifyWrite = false; // Inclui os ciclos internos com AS ativo.
        bool resetAsserted = false;
        bool addressError = false;
        bool interruptAcknowledge = false;
        unsigned interruptLevel = 0;
    };

    // As transferências de byte e palavra são distintas: o 68000 possui sinais de seleção de bytes.
    // A CPU controla alinhamento e temporização; o dispositivo pode sinalizar erro de barramento.
    class M68KBus
    {
    public:
        virtual ~M68KBus() = default;
        virtual std::uint8_t readByte(std::uint32_t address) = 0;
        virtual std::uint16_t readWord(std::uint32_t address) = 0;
        virtual bool tryReadByte(std::uint32_t address, std::uint8_t &value)
        {
            value = readByte(address);
            return true;
        }
        // Consulta DTACK na fase de reconhecimento; falso mantém o ciclo em espera.
        virtual bool tryReadWord(std::uint32_t address, std::uint16_t &value)
        {
            value = readWord(address);
            return true;
        }
        // Observa a fase atual sem avançar o tempo dos periféricos.
        virtual void observe(const M68KPins &) {}
        virtual void writeByte(std::uint32_t address, std::uint8_t value) = 0;
        virtual void writeWord(std::uint32_t address, std::uint16_t value) = 0;
        virtual bool tryWriteByte(std::uint32_t address, std::uint8_t value)
        {
            writeByte(address, value);
            return true;
        }
        // Confirma uma escrita na fase de reconhecimento; falso mantém a transferência pendente.
        virtual bool tryWriteWord(std::uint32_t address, std::uint16_t value)
        {
            writeWord(address, value);
            return true;
        }
        // A resposta padrão usa o autovetor; falso prolonga o ciclo de reconhecimento.
        virtual bool tryAcknowledgeInterrupt(unsigned, M68KInterruptResponse &response)
        {
            response = {};
            return true;
        }
    };
}