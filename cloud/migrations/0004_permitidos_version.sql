-- La versión de los permitidos sube en la misma sentencia que cambia la lista (disparadores de
-- SQLite): si algo falla, no queda una lista nueva con la versión vieja. Antes la subía el Worker
-- con una segunda consulta aparte.
-- La fila de la versión se crea si falta (con 0); los disparadores la crean también si alguien la
-- borra. Sin «OR REPLACE» dentro de los disparadores: SQLite usa ahí el «OR IGNORE» de la sentencia
-- de afuera (la de aprobar) y la versión dejaría de subir.
INSERT OR IGNORE INTO permitidos_version (id, n) VALUES (1, 0);

CREATE TRIGGER permitidos_alta AFTER INSERT ON permitidos
BEGIN
  UPDATE permitidos_version SET n = n + 1 WHERE id = 1;
  INSERT INTO permitidos_version (id, n)
  SELECT 1, 1 WHERE NOT EXISTS (SELECT 1 FROM permitidos_version WHERE id = 1);
END;

CREATE TRIGGER permitidos_baja AFTER DELETE ON permitidos
BEGIN
  UPDATE permitidos_version SET n = n + 1 WHERE id = 1;
  INSERT INTO permitidos_version (id, n)
  SELECT 1, 1 WHERE NOT EXISTS (SELECT 1 FROM permitidos_version WHERE id = 1);
END;

CREATE TRIGGER permitidos_cambio AFTER UPDATE ON permitidos
BEGIN
  UPDATE permitidos_version SET n = n + 1 WHERE id = 1;
  INSERT INTO permitidos_version (id, n)
  SELECT 1, 1 WHERE NOT EXISTS (SELECT 1 FROM permitidos_version WHERE id = 1);
END;
