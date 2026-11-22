-- SiBlock: esquema inicial. Sin IPs, sin MACs, sin texto libre; fechas solo como día
-- (`dia` = días desde 1970 en UTC). `instalacion` = SHA-256(ID_PEPPER ‖ número), 32 hex.
CREATE TABLE reportes_modem (instalacion TEXT PRIMARY KEY, origen TEXT NOT NULL CHECK (origen IN ('pagina','panel')),
  pais TEXT NOT NULL, asn INTEGER, proveedor TEXT NOT NULL, fabricante TEXT NOT NULL,
  dns_ok INTEGER NOT NULL CHECK (dns_ok IN (0,1)), menu TEXT NOT NULL, dia INTEGER NOT NULL);
CREATE TABLE reportes_bloqueo (instalacion TEXT NOT NULL, dominio TEXT NOT NULL, motivo TEXT NOT NULL,
  origen TEXT NOT NULL, dia INTEGER NOT NULL, PRIMARY KEY (instalacion, dominio));
CREATE INDEX reportes_bloqueo_dominio ON reportes_bloqueo (dominio);
CREATE TABLE placas (instalacion TEXT PRIMARY KEY, dia INTEGER NOT NULL, version TEXT NOT NULL, pais TEXT NOT NULL);
CREATE INDEX placas_dia ON placas (dia);
CREATE TABLE bloqueadas (dia INTEGER NOT NULL, instalacion TEXT NOT NULL, total INTEGER NOT NULL, PRIMARY KEY (dia, instalacion));
CREATE TABLE pasos (dia INTEGER NOT NULL, paso TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (dia, paso));
CREATE TABLE temas (dia INTEGER NOT NULL, tema TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (dia, tema));
CREATE TABLE contacto_dia (dia INTEGER PRIMARY KEY, n INTEGER NOT NULL);
