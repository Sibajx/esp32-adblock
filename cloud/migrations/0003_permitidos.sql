-- Lista de permitidos de la comunidad: sitios que el dueño revisó a mano (a partir de los reportes
-- «No debió bloquearse») y aprobó. Solo el nombre del sitio y el día en que se aprobó
-- (`aprobado_en` = días desde 1970 en UTC). El tope de 500 lo pone el Worker.
CREATE TABLE permitidos (dominio TEXT PRIMARY KEY, aprobado_en INTEGER NOT NULL);
-- Una sola fila: un número que sube cada vez que la lista cambia (aprobar o quitar). Las placas lo
-- reciben como `version`.
CREATE TABLE permitidos_version (id INTEGER PRIMARY KEY CHECK (id = 1), n INTEGER NOT NULL);
