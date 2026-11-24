// La température de la puce du Mac (fork pour macOS, 2026-11-22), pour l'affichage en direct de l'application.
#ifndef GULLIVER_HOST_THERMAL_H
#define GULLIVER_HOST_THERMAL_H

#include <string>
#include <vector>

namespace gulliver
{
    struct chip_thermal
    {
        // L'état thermique de macOS (NSProcessInfo.thermalState) : nominal, fair, serious, critical.
        std::string state = "nominal";
        // La plus chaude et la moyenne des sondes de la puce (« PMU tdie »), en degrés ; -1 si aucune n'a répondu.
        double max_c = -1;
        double mean_c = -1;
        int sensors = 0;
    };

    /*!
        La lecture la plus récente, refaite au plus toutes les deux secondes (les sondes passent par
        IOHIDEventSystemClient, une interface interne d'Apple, sans droits d'administrateur ; celle de
        macos/temperature).  Sûre depuis plusieurs fils.
    !*/
    chip_thermal read_chip_thermal();

    struct thermal_sample
    {
        long long time = 0;     // secondes depuis 1970
        double max_c = -1;
        double mean_c = -1;
        std::string state;
        // La charge moyenne depuis le relevé précédent, en % (-1 inconnue) : le processeur (tous ses cœurs, par leurs
        // compteurs de temps) et le GPU (« Device Utilization % », lu toutes les dix secondes).
        double cpu = -1;
        double gpu = -1;
    };

    /*!
        L'historique de la température et de la charge (fork pour macOS, 2026-11-22) : start_thermal_history() lance,
        une seule fois, un fil qui relève read_chip_thermal() toutes les thermal_period secondes (cinq minutes, demande
        de l'humain), avec la charge moyenne du processeur et du GPU sur la période, et garde les sept derniers jours
        (2026-11-24, demande de l'humain : les périodes de 3 et 7 jours de la page) ; thermal_history() rend les relevés
        des minutes dernières minutes, du plus ancien au plus récent.  Avec un fichier (thermal.tsv du dossier de
        données), les relevés y sont ajoutés un par ligne et relus au démarrage : un redémarrage du service ne les
        efface plus.
    !*/
    const int thermal_period = 300;
    const int thermal_keep_minutes = 7 * 24 * 60;
    void start_thermal_history(const std::string& file = std::string());
    std::vector<thermal_sample> thermal_history(int minutes);
}

#endif
