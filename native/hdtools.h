/* Copie de fichiers sur un disque dur Amiga (.hdf), sans passer par l'Amiga (même rôle que « Fichiers… » de la
 * version web) : tables de partitions RDB ou partition seule, systèmes OFS et FFS (DOS\0 à DOS\3). Les archives
 * .lha (jeux WHDLoad) et .zip sont décompressées, les disquettes .adf (et .adz) ouvertes : chacune arrive dans un
 * dossier à son nom, avec l'icône de la disquette. Rien n'est écrit sur le disque si la copie échoue (place,
 * partition non modifiable…). Module commun aux applications Android, Mac et PC. */
#ifndef HDTOOLS_H
#define HDTOOLS_H
#ifdef __cplusplus
extern "C" {
#endif

/* Partitions du disque, une par ligne : « nom\tvolume\tsystème\toctets libres\tmodifiable (1/0)\tmotif ».
 * Renvoie leur nombre, -1 si le disque est illisible (motif dans out). Textes en UTF-8. */
int hdt_partitions(const char *hdf, char *out, int outLen);

/* Copie sur la partition part (indice dans hdt_partitions).
 * Pour chaque source src[i] : si dst[i] est non vide, le fichier est copié tel quel à ce chemin du disque
 * (« C/WHDLoad ») ; sinon il va dans le dossier dest (« Jeux », vide : racine), décompressé ou ouvert si unpack.
 * Renvoie le nombre de fichiers copiés, -1 en cas d'échec (le disque n'est alors pas modifié) ; report reçoit le
 * compte rendu en UTF-8 (fichiers ignorés compris). */
int hdt_copy(const char *hdf, int part, const char *dest, const char *const *src, const char *const *dst, int n,
             int unpack, char *report, int reportLen);

#ifdef __cplusplus
}
#endif
#endif
