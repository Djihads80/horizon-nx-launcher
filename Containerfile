# The toolchain for the Horizon Switch launcher: devkitA64 for Switch, the OpenGL portlibs
# Borealis draws with, and libcurl for the mod browser and Microsoft sign-in.
#
#   docker build -t horizon-launcher-toolchain -f switch-launcher/Containerfile switch-launcher
FROM devkitpro/devkita64:latest

RUN dkp-pacman -Sy --noconfirm --needed && \
    dkp-pacman -S --noconfirm --needed --ask=4 \
        switch-glfw switch-mesa switch-glm switch-curl switch-zlib \
        && dkp-pacman -Scc --noconfirm
