#pragma once

#include <QDateTime>

#include <optional>

namespace satcfdi::vencimientoefirma {

// Vencimiento de e.firma (T014.3 D2, D3). Reglas puras, sin reloj ni E/S.

inline constexpr int kUmbralAviso = 30;    // dias: badge y primer aviso
inline constexpr int kUmbralUrgente = 7;   // dias: segundo aviso

// Dias completos que faltan para `vigenteHasta` vistos en `ahoraUtc`
// (segundos restantes / 86400, truncado): 0 = vence en menos de 24 h.
// nullopt si ya vencio (vigenteHasta <= ahora), si alguna fecha es invalida o
// si faltan mas de kUmbralAviso dias. Solo aplica a credenciales Lista (lo
// decide quien llama).
std::optional<int> diasParaVencer(const QDateTime& vigenteHasta, const QDateTime& ahoraUtc);

// Umbral alcanzado para `dias` (resultado de diasParaVencer): 7 si dias <= 7;
// 30 si dias <= 30. Solo el mas urgente: al llegar tarde (p. ej. 5 dias sin
// haber avisado a 30) se avisa una vez, por 7.
int umbralPara(int dias);

} // namespace satcfdi::vencimientoefirma
