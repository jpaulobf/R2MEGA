#pragma once

namespace R2MEGA::Systems
{
    // Coordenará a ligação do núcleo com entrada, áudio, temporização e interface.
    class Engine
    {
    public:
        // Executa a aplicação e retorna zero em caso de sucesso.
        int run();
    };
}
