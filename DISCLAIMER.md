# Legal Disclaimer & Intellectual Property Notice

> **Read this before using this repository, running the emulator, probing, or modifying any device.**

---

## 1. Non-Affiliation Disclaimer

N2660Emu is an independent, non-commercial, open-source research and educational project developed by individual contributors.

* This project is not affiliated with, endorsed by, sponsored by, or in any way associated with Nokia Corporation, HMD Global Oy, Unisoc (Shanghai) Technologies Co., Ltd., or any of their respective subsidiaries or affiliates.
* The official Nokia website can be found at [nokia.com](https://www.nokia.com).
* The official HMD website can be found at [hmd.com](https://www.hmd.com).
* The official Unisoc website can be found at [unisoc.com](https://www.unisoc.com).

---

## 2. Trademark Notice & Nominative Fair Use

* **"Nokia"**, **"Nokia 2660"**, **"Nokia 2660 Flip"**, and related model designations are registered trademarks of Nokia Corporation and/or HMD Global Oy.
* **"Unisoc"**, **"Spreadtrum"**, **"T107"**, **"UMS9117"**, and related platform identifiers are trademarks or registered trademarks of Unisoc (Shanghai) Technologies Co., Ltd.
* Any use of these trademarks, trade names, or model identifiers within this repository, its documentation, or its source code is strictly for nominative, descriptive, and identification purposes only—specifically to indicate the target hardware architecture, peripheral specifications, and interoperability objectives of this emulator.
* Such use falls under the statutory exceptions of Section 30(2)(d) of the Indian Trade Marks Act, 1999 and the doctrine of Nominative Fair Use under applicable international trademark laws.
* This project does not use any official corporate logos, stylized wordmarks, or trade dress of Nokia, HMD Global, or Unisoc.

---

## 3. Reverse Engineering & Interoperability

* Research and reverse engineering conducted in connection with this project are performed in strict accordance with statutory interoperability exceptions, including Section 52(1)(ab) and Section 52(1)(ac) of the Indian Copyright Act, 1957, as amended by the Copyright (Amendment) Act, 2012.
* The hardware registers, memory maps, bitmasks, timing parameters, and interface definitions documented in this project represent uncopyrightable functional ideas, systems, and methods of operation under copyright law (Idea-Expression Dichotomy).
* All emulator peripheral models, machine abstractions, and test payloads in this repository are original, independent implementations written from scratch without copying proprietary source code or disassembly listings.

---

## 4. Firmware Redistribution Policy

* **No Proprietary Firmware Included:** This repository does not contain, distribute, or host any proprietary firmware binaries, BootROM images, bootloaders (`fdl1`, `fdl2`, `boot0`, `boot1`), kernel binaries, or operating system code (MOCOR).
* **User-Supplied Firmware Model:** Users who wish to run or test proprietary software on this emulator must legally obtain and extract their own firmware dumps from hardware they lawfully possess.
* The maintainers of this project do not provide assistance in obtaining copyrighted firmware images through unauthorized channels.

---

## 5. Hardware Safety, Warranty & Operational Risk

* **Active Development:** This project is currently in active development. Tools, reverse-engineered register maps, payloads, and documentation are continuously evolving; some things might be incomplete, experimental, or not work as intended.
* **No Warranty / No Guarantee:** Everything in this repository is provided **AS IS**, with **no warranty** and **no guarantee** of accuracy, completeness, safety, or fitness for any purpose. Tools, addresses, PAC files, and procedures may be wrong, incomplete, or unsafe for your exact hardware revision or software build. Firmware samples documented here may not match a TA-1480 or your unit.
* **You Are Responsible for Your Device:** The authors and contributors are **not responsible** for bricked, damaged, or unstable devices, voided warranties, data loss, lost cellular service, or any other harm. Flashing, download mode, UART, test points, and custom images can permanently destroy a phone. If you proceed, you do so entirely at your own risk.
* **Confidentiality & Personal Data:** Do not publish IMEI, MAC addresses, personal data, or secret calibration dumps (`nvitem.bin`).
* **Safety Best Practices:** Prefer a spare device. Keep backups of stock firmware when possible. Double-check model, PAC product string, and hashes before any write operation.

---

*By using this repository, you acknowledge that you understand these risks and agree that contributors have no liability for your actions or outcomes.*

*Related Documents:*
* License: [GNU General Public License v3.0 (GPL-3.0)](LICENSE)
* Contributing Guidelines: [CONTRIBUTING.md](CONTRIBUTING.md)
* Legal Documentation: [docs/legal-notice.md](docs/legal-notice.md)
