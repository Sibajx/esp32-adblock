-- Respuestas automáticas de Contáctanos: una por dirección y día, con tope diario propio.
-- `destino` = SHA-256(ID_PEPPER ‖ correo en minúsculas), 32 hex; nunca el correo. Se borra al día siguiente.
CREATE TABLE respuestas_dia (dia INTEGER NOT NULL, destino TEXT NOT NULL, PRIMARY KEY (dia, destino));
