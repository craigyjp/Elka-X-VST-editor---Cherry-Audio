const char* VERSION = "V1.1";
const float QUADRASEMITONES[128] = { -24.00, -24.00, -23.00, -23.00, -22.00, -22.00, -22.00, -21.00, -21.00, -21.00, -20.00, -20.00, -19.00, -19.00, -19.00, -18.00, -18.00, -18.00, -17.00, -17.00, -16.00, -16.00, -16.00, -15.00, -15.00, -15.00, -14.00, -14.00, -13.00, -13.00, -13.00, -12.00, -12.00, -12.00, -11.00, -11.00, -10.00, -10.00, -10.00, -9.00, -9.00, -9.00, -8.00, -8.00, -7.00, -7.00, -7.00, -6.00, -6.00, -5.00, -5.00, -5.00, -4.00, -4.00, -4.00, -3.00, -3.00, -2.00, -2.00, -2.00, -1.00, -1.00, -1.00, 0.00, 0.00, 1.00, 1.00, 1.00, 2.00, 2.00, 2.00, 3.00, 3.00, 4.00, 4.00, 4.00, 5.00, 5.00, 5.00, 6.00, 6.00, 7.00, 7.00, 7.00, 8.00, 8.00, 9.00, 9.00, 9.00, 10.00, 10.00, 10.00, 11.00, 11.00, 12.00, 12.00, 12.00, 13.00, 13.00, 13.00, 14.00, 14.00, 15.00, 15.00, 15.00, 16.00, 16.00, 16.00, 17.00, 17.00, 18.00, 18.00, 18.00, 19.00, 19.00, 19.00, 20.00, 20.00, 21.00, 21.00, 21.00, 22.00, 22.00, 22.00, 23.00, 23.00, 24.00, 24.00};
const float QUADRABENDPITCH[128] = {0.0, 0.0, 0.0, 0.01, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.09, 0.11, 0.13, 0.15, 0.17, 0.19, 0.22, 0.24, 0.27, 0.30, 0.33, 0.36, 0.39, 0.43, 0.47, 0.50, 0.54, 0.58, 0.63, 0.67, 0.71, 0.76, 0.81, 0.86, 0.91, 0.96, 1.02, 1.07, 1.13, 1.19, 1.25, 1.31, 1.38, 1.44, 1.51, 1.57, 1.64, 1.71, 1.79, 1.86, 1.94, 2.01, 2.09, 2.17, 2.25, 2.33, 2.42, 2.50, 2.59, 2.68, 2.77, 2.86, 2.95, 3.05, 3.14, 3.24, 3.34, 3.44, 3.54, 3.65, 3.75, 3.86, 3.96, 4.07, 4.19, 4.30, 4.41, 4.53, 4.64, 4.76, 4.88, 5.00, 5.13, 5.25, 5.38, 5.50, 5.63, 5.76, 5.89, 6.03, 6.16, 6.30, 6.43, 6.57, 6.71, 6.86, 7.00, 7.15, 7.29, 7.44, 7.59, 7.74, 7.89, 8.05, 8.20, 8.36, 8.52, 8.68, 8.84, 9.00, 9.17, 9.33, 9.50, 9.67, 9.84, 10.01, 10.18, 10.36, 10.54, 10.71, 10.89, 11.07, 11.26, 11.44, 11.63, 11.81, 12.0};
const float QUADRA100LOG[128] = {0.00, 0.00, 0.00, 0.1, 0.1, 0.2, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.5, 2.7, 3.0, 3.3, 3.6, 3.9, 4.2, 4.5, 4.9, 5.2, 5.6, 6.0, 6.3, 6.8, 7.2, 7.6, 8.0, 8.5, 9.0, 9.4, 9.9, 10.04, 10.9, 11.5, 12.0, 12.6, 13.1, 13.7, 14.3, 14.9, 15.5, 16.1, 16.8, 17.4, 18.1, 18.8, 19.4, 20.1, 20.9, 21.6, 22.3, 23.1, 23.8, 24.6, 25.4, 26.2, 27.0, 27.8, 28.7, 29.5, 30.4, 31.3, 32.1, 33.0, 34.0, 34.9, 35.8, 36.8, 37.7, 38.7, 39.7, 40.7, 41.7, 42.7, 43.7, 44.8, 45.9, 46.9, 48.0, 49.1, 50.2, 51.3, 52.5, 53.6, 54.8, 56.0, 57.1, 58.3, 59.5, 60.8, 62.0, 63.2, 64.5, 65.8, 67.1, 68.4, 69.7, 71.0, 72.3, 73.7, 75.0, 76.4, 77.8, 79.2, 80.6, 82.0, 83.4, 84.9, 86.3, 87.8, 89.3, 90.8, 92.3, 93.8, 95.3, 96.9, 98.4, 100.0};
const float QUADRA100[128] = {0.00, 0.8, 1.6, 2.4, 3.1, 3.9, 4.7, 5.5, 6.3, 7.1, 7.9, 8.7, 9.4, 10.2, 11.0, 11.8, 12.6, 13.4, 14.2, 15.0, 15.7, 16.5, 17.3, 18.1, 18.9, 19.7, 20.5, 21.3, 22.0, 22.8, 23.6, 24.4, 25.2, 26.0, 26.8, 27.6, 28.3, 29.1, 29.9, 30.7, 31.5, 32.3, 33.1, 33.9, 34.6, 35.4, 36.2, 37.0, 37.8, 38.6, 39.4, 40.2, 40.9, 41.7, 42.5, 43.3, 44.1, 44.9, 45.7, 46.5, 47.2, 48.0, 48.8, 49.6, 50.4, 51.2, 52.0, 52.8, 53.5, 54.3, 55.1, 55.9, 56.7, 57.5, 58.3, 59.1, 59.8, 60.6, 61.4, 62.2, 63.0, 63.8, 64.6, 65.4, 66.1, 66.9, 67.7, 68.5, 69.3, 70.1, 70.9, 71.7, 72.4, 73.2, 74.0, 74.8, 75.6, 76.4, 77.2, 78.0, 78.7, 79.5, 80.3, 81.1, 81.9, 82.7, 83.5, 84.3, 85.0, 85.8, 86.6, 87.4, 88.2, 89.0, 89.8, 90.6, 91.3, 92.1, 92.9, 93.7, 94.5, 95.3, 96.1, 96.9, 97.6, 98.4, 99.2, 100.0};
const float QUADRAEVCO2TUNE[128] = {-12.0, -11.8, -11.6, -11.4, -11.2, -11.1, -10.9, -10.7, -10.5, -10.3, -10.1, -9.9, -9.7, -9.5, -9.4, -9.2, -9.0, -8.8, -8.6, -8.4, -8.2, -8.0, -7.8, -7.7, -7.5, -7.3, -7.1, -6.9, -6.7, -6.5, -6.3, -6.1, -6.0, -5.8, -5.6, -5.4, -5.2, -5.0, -4.8, -4.6, -4.4, -4.3, -4.1, -3.9, -3.7, -3.5, -3.3, -3.1, -2.9, -2.7, -2.6, -2.4, -2.2, -2.0, -1.8, -1.6, -1.4, -1.2, -1.0, -0.9, -0.7, -0.5, -0.3, -0.1, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.4, 2.6, 2.7, 2.9, 3.1, 3.3, 3.5, 3.7, 3.9, 4.1, 4.3, 4.4, 4.6, 4.8, 5.0, 5.2, 5.4, 5.6, 5.8, 6.0, 6.1, 6.3, 6.5, 6.7, 6.9, 7.1, 7.3, 7.5, 7.7, 7.8, 8.0, 8.2, 8.4, 8.6, 8.8, 9.0, 9.2, 9.4, 9.5, 9.7, 9.9, 10.1, 10.3, 10.5, 10.7, 10.9, 11.1, 11.2, 11.4, 11.6, 11.8, 12.0};
const float QUADRAETUNE[128] = {-1.00, -0.98, -0.97, -0.95, -0.94, -0.92, -0.91, -0.89, -0.87, -0.86, -0.84, -0.83, -0.81, -0.80, -0.78, -0.76, -0.75, -0.73, -0.72, -0.70, -0.69, -0.67, -0.65, -0.64, -0.62, -0.61, -0.59, -0.57, -0.56, -0.54, -0.53, -0.51, -0.50, -0.48, -0.46, -0.45, -0.43, -0.42, -0.40, -0.39, -0.37, -0.35, -0.34, -0.32, -0.31, -0.29, -0.28, -0.26, -0.24, -0.23, -0.21, -0.20, -0.18, -0.17, -0.15, -0.13, -0.12, -0.10, -0.09, -0.07, -0.06, -0.04, -0.02, -0.01, 0.01, 0.02, 0.04, 0.06, 0.07, 0.09, 0.10, 0.12, 0.13, 0.15, 0.17, 0.18, 0.20, 0.21, 0.23, 0.24, 0.26, 0.28, 0.29, 0.31, 0.32, 0.34, 0.35, 0.37, 0.39, 0.40, 0.42, 0.43, 0.45, 0.46, 0.48, 0.50, 0.51, 0.53, 0.54, 0.56, 0.57, 0.59, 0.61, 0.62, 0.64, 0.65, 0.67, 0.69, 0.70, 0.72, 0.73, 0.75, 0.76, 0.78, 0.80, 0.81, 0.83, 0.84, 0.86, 0.87, 0.89, 0.91, 0.92, 0.94, 0.95, 0.97, 0.98, 1.00};
const float QUADRALEADATTACK[128] = {3.0, 3.06, 3.25, 3.56, 3.99, 4.55, 5.03, 5.23, 6.03, 6.96, 8.01, 9.18, 10.48, 11.90, 13.45, 15.12, 16.91, 18.82, 20.86, 23.03, 25.31, 27.73, 30.26, 35.70, 38.60, 41.63, 44.79, 48.06, 51.46, 54.99, 58.63, 62.40, 66.30, 70.32, 74.46, 78.72, 83.11, 87.62, 92.26, 97.02, 101.90, 106.91, 112.04, 117.29, 122.67, 128.17, 133.80, 139.55, 145.42, 151.42, 157.54, 163.78, 170.15, 176.64, 183.25, 189.99, 196.85, 203.83, 210.94, 218.17, 225.53, 233.01, 240.61, 248.34, 256.19, 264.16, 272.26, 280.48, 288.83, 297.30, 305.89, 314.60, 323.44, 332.41, 341.49, 350.70, 360.04, 369.50, 379.08, 388.78, 398.61, 408.56, 418.64, 428.84, 439.16, 449.61, 460.18, 470.87, 481.69, 492.63, 503.69, 514.88, 526.19, 537.63, 549.19, 560.87, 572.68, 584.61, 596.66, 608.84, 621.14, 633.57, 646.11, 658.79, 671.58, 684.50, 697.54, 710.71, 724.00, 737.41, 750.95, 764.61, 778.40, 792.30, 806.34, 820.49, 834.77, 849.17, 863.70, 878.35, 893.12, 908.02, 923.04, 938.19, 953.45, 968.85, 984.36, 1000.00};
const float QUADRALEADDECAY[128] = {10.0, 10.25, 10.99, 12.23, 13.96, 16.18, 18.91, 22.12, 25.83, 30.04, 34.74, 39.93, 45.62, 51.81, 58.49, 65.66, 73.33, 81.49, 90.15, 99.30, 108.95, 119.09, 129.73, 140.86, 152.49, 164.61, 177.23, 190.34, 203.95, 218.05, 232.64, 247.73, 263.32, 279.40, 295.97, 313.04, 330.61, 348.66, 367.22, 386.27, 405.81, 425.85, 446.38, 467.41, 488.93, 510.95, 533.46, 556.46, 579.96, 603.96, 628.45, 653.44, 678.92, 704.89, 731.36, 758.33, 785.79, 813.74, 842.19, 871.13, 900.57, 930.50, 960.93, 991.85, 1023.27, 1055.18, 1087.59, 1120.49, 1153.89, 1187.78, 1222.16, 1257.05, 1292.42, 1328.29, 1364.66, 1401.52, 1438.72, 1476.72, 1515.06, 1553.90, 1593.24, 1633.06, 1673.39, 1714.20, 1755.52, 1797.32, 1839.63, 1882.42, 1925.71, 1969.50, 2013.78, 2058.56, 2103.83, 2149.59, 2195.85, 2242.61, 2289.86, 2337.60, 2385.84, 2434.58, 2483.80, 2533.53, 2583.75, 2634.46, 2685.67, 2737.37, 2789.57, 2842.46, 2895.45, 2949.13, 3003.30, 3057.98, 3113.14, 3168.80, 3224.96, 3281.61, 3338.75, 3396.39, 3454.53, 3513.16, 3572.28, 3631.90, 3692.01, 3752.62, 3813.72, 3875.32, 3937.41, 4000.00};
const float QUADRALEADRELEASE[128] = {10.0, 10.15, 10.62, 11.39, 12.47, 13.86, 15.56, 17.56, 19.88, 22.50, 25.44, 28.68, 32.23, 36.09, 40.26, 44.74, 49.52, 54.62, 60.02, 65.73, 71.75, 78.08, 84.72, 91.67, 98.92, 106.49, 114.36, 122.54, 131.03, 138.83, 148.94, 158.36, 168.09, 178.12, 188.46, 199.12, 210.08, 221.35, 232.93, 244.81, 257.01, 269.51, 282.33, 295.45, 308.88, 322.62, 336.67, 351.03, 365.69, 380.67, 395.95, 411.54, 427.44, 443.65, 460.17, 477.00, 494.14, 511.58, 529.34, 547.40, 565.77, 585.45, 603.44, 622.74, 642.34, 662.26, 682.48, 703.01, 723.85, 745.00, 766.46, 788.23, 810.31, 832.69, 855.39, 878.39, 901.70, 925.32, 949.25, 973.49, 998.03, 1022.89, 1048.05, 1073.53, 1099.31, 1125.40, 1151.80, 1178.50, 1205.52, 1232.85, 1260.48, 1288.42, 1316.67, 1345.24, 1374.10, 1403.28, 1432.77, 1462.56, 1492.67, 1523.08, 1553.80, 1584.83, 1616.17, 1647.82, 1679.78, 1712.04, 1744.62, 1777.50, 1810.69, 1844.19, 1878.00, 1912.12, 1946.55, 1981.28, 2016.33, 2051.68, 2087.34, 2123.59, 2159.59, 2196.18, 2233.08, 2270.28, 2307.80, 2345.62, 2383.75, 2422.19, 2460.94, 2500.00};
const float QUADRACUTOFF[128] = {16.00, 16.00, 16.00, 16.00, 16.00, 16.00, 16.00, 16.01, 16.02, 16.03, 16.05, 16.08, 16.12, 16.18, 16.26, 16.37, 16.51, 16.69, 16.91, 17.20, 17.55, 17.98, 18.49, 19.11, 19.85, 20.72, 21.75, 22.94, 24.33, 25.92, 27.76, 29.85, 32.23, 34.93, 37.98, 41.41, 45.25, 49.55, 54.33, 59.65, 65.54, 72.05, 79.23, 87.12, 95.79, 105.27, 115.65, 126.96, 139.27, 152.66, 167.19, 182.92, 199.94, 218.32, 238.14, 259.49, 282.44, 307.10, 333.55, 361.88, 392.20, 424.62, 459.23, 496.14, 535.48, 577.35, 621.88, 669.19, 719.41, 772.68, 829.12, 888.89, 952.12, 1018.95, 1089.56, 1164.08, 1242.69, 1325.55, 1412.82, 1504.68, 1601.32, 1702.91, 1809.64, 1921.71, 2039.31, 2162.65, 2291.93, 2427.37, 2569.17, 2717.57, 2972.80, 3035.07, 3204.64, 3381.75, 3566.64, 3759.56, 3960.78, 4170.56, 4389.18, 4616.90, 4854.01, 5100.80, 5357.56, 5624.58, 5902.18, 6190.66, 6490.35, 6801.56, 7124.63, 7459.88, 7807.67, 8168.33, 8542.23, 8929.72, 9331.18, 9746.97, 10177.48, 10623.09, 11084.20, 11561.21, 12054.52, 12564.55, 13091.73, 13636.48, 14199.23, 14780.44, 15380.54, 16000.00};
const float QUADRAPORT[128] = {0.00, 0.06, 0.25, 0.56, 0.99, 1.55, 2.23, 3.04, 3.97, 5.02, 6.20, 7.50, 8.93, 10.48, 12.15, 13.95, 15.87, 17.92, 20.09, 22.38, 24.80, 27.34, 30.01, 32.80, 35.71, 38.75, 41.91, 45.20, 48.61, 52.14, 55.80, 59.58, 63.49, 67.52, 71.67, 75.95, 80.35, 84.88, 89.53, 94.30, 99.20, 104.22, 109.37, 114.64, 120.03, 125.55, 131.19, 136.96, 142.85, 148.86, 155.00, 161.26, 167.65, 174.16, 180.79, 187.55, 194.43, 201.44, 208.57, 215.82, 223.20, 230.70, 238.33, 246.08, 253.95, 261.95, 270.07, 278.32, 286.69, 295.18, 303.80, 312.54, 321.41, 330.40, 339.51, 348.75, 358.11, 367.60, 377.21, 386.94, 396.80, 406.78, 416.89, 427.12, 437.47, 447.95, 458.55, 469.28, 480.13, 491.10, 502.20, 513.42, 524.77, 536.24, 547.83, 559.55, 571.39, 583.36, 595.45, 607.66, 620.00, 632.46, 645.05, 657.78, 670.59, 683.55, 696.63, 709.84, 723.17, 736.62, 750.20, 763.90, 777.73, 791.68, 805.75, 819.95, 834.27, 848.72, 863.29, 877.98, 892.80, 907.74, 922.81, 938.00, 953.31, 968.75, 984.31, 1000.00};
const float QUADRAEQ[128] = {-9.00, -8.86, -8.72, -8.57, -8.43, -8.29, -8.15, -8.01, -7.87, -7.72, -7.58, -7.44, -7.30, -7.16, -7.02, -6.87, -6.73, -6.59, -6.45, -6.31, -6.17, -6.02, -5.88, -5.74, -5.60, -5.46, -5.31, -5.17, -5.03, -4.89, -4.75, -4.61, -4.46, -4.32, -4.18, -4.04, -3.90, -3.76, -3.61, -3.47, -3.33, -3.19, -3.05, -2.91, -2.76, -2.62, -2.48, -2.34, -2.20, -2.06, -1.91, -1.77, -1.63, -1.49, -1.35, -1.20, -1.06, -0.92, -0.78, -0.64, -0.50, -0.35, -0.21, -0.07, 0.07, 0.21, 0.35, 0.50, 0.64, 0.78, 0.92, 1.06, 1.20, 1.35, 1.49, 1.63, 1.77, 1.91, 2.06, 2.20, 2.34, 2.48, 2.62, 2.76, 2.91, 3.05, 3.19, 3.33, 3.47, 3.61, 3.76, 3.90, 4.04, 4.18, 4.32, 4.46, 4.61, 4.75, 4.89, 5.03, 5.17, 5.31, 5.46, 5.60, 5.74, 5.88, 6.02, 6.17, 6.31, 6.45, 6.59, 6.73, 6.87, 7.02, 7.16, 7.30, 7.44, 7.58, 7.72, 7.87, 8.01, 8.15, 8.29, 8.43, 8.57, 8.72, 8.86, 9.00};
const float QUADRAEBASSDECAY[128] = {150.00, 150.30, 151.20, 152.71, 154.81, 157.52, 160.83, 164.73, 169.24, 174.36, 180.07, 186.38, 193.30, 200.82, 208.94, 217.66, 226.98, 236.90, 247.43, 258.55, 270.82, 282.61, 295.54, 309.07, 323.20, 337.94, 353.27, 369.21, 385.75, 402.89, 420.63, 438.97, 457.92, 477.46, 497.61, 518.36, 539.71, 561.66, 584.21, 607.37, 631.12, 655.48, 680.44, 706.00, 732.16, 758.92, 786.28, 814.25, 842.81, 871.98, 901.75, 932.12, 963.09, 994.67, 1026.84, 1059.62, 1093.00, 1126.98, 1161.56, 1196.74, 1232.52, 1268.91, 1305.89, 1343.48, 1381.67, 1420.46, 1459.85, 1499.84, 1540.44, 1581.64, 1623.43, 1665.83, 1708.83, 1752.43, 1796.64, 1841.44, 1886.85, 1932.85, 1979.46, 2026.67, 2074.48, 2122.90, 2171.91, 2221.53, 2271.74, 2322.56, 2373.98, 2426.00, 2478.63, 2531.85, 2585.67, 2460.10, 2695.13, 2750.76, 2806.99, 2863.82, 2921.26, 2979.29, 3037.93, 3097.17, 3157.01, 3217.45, 3278.49, 3340.13, 3402.38, 3465.22, 3528.67, 3592.72, 3657.37, 3722.62, 3788.48, 3854.93, 3921.99, 3989.65, 4057.91, 4126.77, 4196.23, 4266.29, 4336.96, 4408.22, 4480.09, 4552.56, 4625.63, 4699.30, 4773.57, 4848.45, 4923.92, 5000.00};
const float QUADRAESTRINGSATTACK[128] = {10.00, 10.00, 10.01, 10.02, 10.06, 10.12, 10.23, 10.38, 10.60, 10.88, 11.25, 11.72, 12.30, 13.01, 13.85, 14.84, 16.00, 17.35, 18.89, 20.65, 22.63, 24.86, 27.36, 30.13, 33.20, 36.58, 40.29, 44.35, 48.78, 53.59, 58.81, 64.44, 70.52, 77.06, 84.08, 91.59, 99.62, 108.19, 117.32, 127.03, 137.33, 148.26, 159.82, 172.05, 184.95, 198.56, 212.90, 227.98, 243.82, 260.46, 277.91, 296.19, 315.32, 335.34, 356.25, 378.09, 400.88, 424.63, 449.38, 475.14, 501.95, 529.81, 558.77, 588.83, 620.03, 652.38, 685.92, 720.66, 756.64, 793.87, 832.38, 872.20, 913.35, 955.85, 999.74, 1045.03, 1091.75, 1139.93, 1189.59, 1240.75, 1293.46, 1347.72, 1403.57, 1461.02, 1520.12, 1580.88, 1643.34, 1707.51, 1773.42, 1841.11, 1910.59, 1981.90, 2055.07, 2130.11, 2207.05, 2285.93, 2366.77, 2449.61, 2534.46, 2621.35, 2710.31, 2801.38, 2894.57, 2989.92, 3087.46, 3187.21, 3289.20, 3393.45, 3500.01, 3608.90, 3720.14, 3883.76, 3949.80, 4068.29, 4189.24, 4312.69, 4438.68, 4567.23, 4698.36, 4832.12, 4968.52, 5107.60, 5249.39, 5393.91, 5541.21, 5691.30, 5844.22, 6000.00};
const float QUADRAESTRINGSRELEASE[128] = {10.00, 10.03, 10.19, 10.51, 11.05, 11.84, 12.91, 14.27, 15.97, 18.01, 20.42, 23.23, 26.44, 30.08, 34.17, 38.72, 43.75, 49.27, 55.30, 61.86, 68.95, 76.60, 84.81, 93.61, 102.99, 112.98, 123.59, 134.83, 146.71, 159.25, 172.45, 186.33, 200.89, 216.16, 252.13, 248.83, 266.26, 284.42, 303.34, 323.03, 343.48, 364.71, 386.74, 409.57, 433.20, 457.66, 482.95, 509.07, 536.04, 563.87, 592.56, 622.13, 652.58, 683.92, 716.16, 749.31, 783.37, 818.36, 854.28, 891.15, 928.96, 967.73, 1007.46, 1048.17, 1089.86, 1132.54, 1176.21, 1220.89, 1266.58, 1313.29, 1361.02, 1409.79, 1459.60, 1510.46, 1562.38, 1615.35, 1669.40, 1724.53, 1780.74, 1838.04, 1896.44, 1955.94, 2016.56, 2078.30, 2141.16, 2205.15, 2270.29, 2336.57, 2404.00, 2472.59, 2542.35, 2613.28, 2685.39, 2758.68, 2833.17, 2908.86, 2985.74, 3063.84, 3143.16, 3223.70, 3305.47, 3388.48, 3472.72, 3558.22, 3644.97, 3732.98, 3822.26, 3912.81, 4004.63, 4097.74, 4192.15, 4287.84, 4384.84, 4483.15, 4582.77, 4683.71, 4785.98, 4889.58, 4994.51, 5100.79, 5208.41, 5317.39, 5427.73, 5539.43, 5652.50, 5766.95, 5882.78, 5000.00};
const float QUADRAINITPW[128] = {1.00, 1.8, 2.5, 3.3, 4.1, 4.9, 5.6, 6.4, 7.2, 7.9, 8.7, 9.5, 10.3, 11, 11.8, 12.6, 13.3, 14.1, 14.9, 15.7, 16.4, 17.2, 18, 18.7, 19.5, 20.3, 21.1, 21.8, 22.6, 23.4, 24.1, 24.9, 25.7, 26.5, 27.2, 28, 28.8, 29.6, 30.3, 31.1, 31.9, 32.6, 33.4, 34.2, 35, 35.7, 36.5, 37.3, 38, 38.8, 39.6, 40.4, 41.1, 41.9, 42.7, 43.4, 44.2, 45.0, 45.8, 46.5, 47.3, 48.1, 48.8, 49.6, 50.4, 51.2, 51.9, 52.7, 53.5, 54.2, 55, 55.8, 56.6, 57.3, 58.1, 58.9, 59.6, 60.4, 61.2, 62, 62.7, 63.5, 64.3, 65, 65.8, 66.6, 67.4, 68.1, 68.9, 69.7, 70.4, 71.2, 72, 72.8, 73.5, 74.3, 75.1, 75.9, 76.6, 77.4, 78.2, 78.9, 79.7, 80.5, 81.3, 82, 82.8, 83.6, 84.3, 85.1, 85.9, 86.7, 87.4, 88.2, 89, 89.7, 90.5, 91.3, 92.1, 92.8, 93.6, 94.4, 95.1, 95.9, 96.7, 97.5, 98.2, 99};
const float QUADRAPOLYATTACK[128] = {10.0, 10.00, 10.00, 10.01, 10.03, 10.06, 10.11, 10.19, 10.30, 10.44, 10.63, 10.86, 11.15, 11.50, 11.92, 12.42, 13, 13.67, 14.44, 15.32, 16.31, 17.42, 18.66, 20.05, 21.58, 23.27, 25.12, 27.15, 29.36, 31.76, 34.36, 37.18, 40.21, 43.47, 46.98, 50.73, 54.74, 59.02, 63.57, 68.42, 73.56, 79.01, 84.79, 90.89, 97.33, 104.12, 111.28, 118.81, 126.72, 135.02, 143.73, 152.85, 162.41, 172.40, 182.84, 193.74, 205.11, 216.97, 229.32, 242.18, 255.56, 269.47, 283.92, 298.93, 314.50, 330.65, 347.39, 364.74, 382.70, 401.28, 420.51, 440.38, 460.92, 482.14, 504.04, 526.65, 549.97, 574.02, 598.81, 624.35, 650.66, 677.74, 705.62, 734.30, 763.80, 794.13, 825.31, 857.34, 890.24, 924.03, 958.71, 994.31, 1030.83, 1068.28, 1106.69, 1146.07, 1186.42, 1227.77, 1270.12, 1313.50, 1357.90, 1403.36, 1449.88, 1497.47, 1546.16, 1595.95, 1646.86, 1698.90, 1752.09, 1806.44, 1861.97, 1918.69, 1976.61, 2035.76, 2096.13, 2157.76, 2220.64, 2284.81, 2350.27, 2417.03, 2485.12, 2554.54, 2625.32, 2697.46, 2770.99, 2845.91, 2922.24, 3000};
const float QUADRAPOLYDECAY[128] = {200.0, 200.02, 200.12, 200.33, 200.67, 201.17, 201.84, 202.71, 203.78, 205.08, 206.61, 208.39, 210.43, 212.74, 215.33, 218.22, 221.41, 224.91, 228.74, 232.90, 237.40, 242.25, 247.46, 253.04, 258.99, 265.33, 272.06, 279.19, 286.73, 294.68, 303.06, 311.86, 321.10, 330.79, 340.92, 351.51, 362.57, 374.09, 386.09, 398.58, 411.56, 425.03, 439.00, 453.48, 468.48, 483.99, 500.03, 516.61, 533.72, 551.37, 569.57, 588.33, 607.65, 627.53, 647.98, 669.01, 690.62, 712.82, 745.60, 758.99, 782.98, 807.57, 832.78, 858.61, 885.05, 912.13, 939.83, 968.18, 997.16, 1026.79, 1057.08, 1088.01, 1119.61, 1151.88, 1184.81, 1218.42, 1252.71, 1287.68, 1323.34, 1359.69, 1396.74, 1434.49, 1472.94, 1512.11, 1551.99, 1592.58, 1633.91, 1675.95, 1718.73, 1762.25, 1806.50, 1851.50, 1897.24, 1943.74, 1990.99, 2039.01, 2087.78, 2137.33, 2187.65, 2238.74, 2290.62, 2343.27, 2396.72, 2450.96, 2505.99, 2561.82, 2618.46, 2675.90, 2734.16, 2793.23, 2853.11, 2913.82, 2975.36, 3037.73, 3100.92, 3164.96, 3229.84, 3295.56, 3362.13, 3429.55, 3497.82, 3566.96, 3636.96, 3707.82, 3779.55, 3852.16, 3925.64, 4000};
const float QUADRAPOLYRELEASE[128] = {200.0, 200.03, 200.18, 200.50, 201.02, 201.78, 202.81, 204.14, 205.78, 207.75, 210.09, 212.81, 215.92, 219.44, 223.40, 227.81, 232.68, 238.02, 243.86, 250.21, 257.08, 264.49, 272.44, 280.95, 290.04, 299.72, 309.99, 320.87, 332.38, 344.52, 357.30, 370.74, 384.84, 399.62, 415.09, 431.25, 448.13, 465.72, 484.04, 503.10, 522.90, 543.46, 564.79, 586.89, 609.78, 633.46, 657.94, 683.24, 709.36, 736.30, 764.08, 729.71, 822.20, 852.54, 883.76, 915.86, 948.84, 982.72, 1017.50, 1053.20, 1089.81, 1127.35, 1165.82, 1205.24, 1245.61, 1286.93, 1329.22, 1372.48, 1416.72, 1461.95, 1508.17, 1555.39, 1603.62, 1652.87, 1703.14, 1754.43, 1806.77, 1860.14, 1914.57, 1970.05, 2026.60, 2084.22, 2142.91, 2202.69, 2263.56, 2325.52, 2388.59, 2452.77, 2518.06, 2584.48, 2652.03, 2720.71, 2790.53, 2861.50, 2933.62, 3006.90, 3081.36, 3156.98, 3223.78, 3311.76, 3390.94, 3471.31, 3552.89, 3635.67, 3719.67, 3804.89, 3891.33, 3979.01, 4067.93, 4158.08, 4249.49, 4342.15, 4436.08, 4531.27, 4627.73, 4725.47, 4824.49, 4924.80, 5026.40, 5129.31, 5223.52, 5339.04, 5445.88, 5554.04, 5663.52, 5774.34, 5886.50, 6000};
const float QUADRAPHASER[128] = {0.10, 0.14, 0.18, 0.22, 0.25, 0.29, 0.33, 0.37, 0.41, 0.45, 0.49, 0.52, 0.56, 0.60, 0.64, 0.68, 0.72, 0.76, 0.79, 0.83, 0.87, 0.91, 0.95, 0.99, 1.03, 1.06, 1.10, 1.14, 1.18, 1.22, 1.26, 1.30, 1.33, 1.37, 1.41, 1.45, 1.49, 1.53, 1.57, 1.60, 1.64, 1.68, 1.72, 1.76, 1.80, 1.84, 1.87, 1.91, 1.95, 1.99, 2.03, 2.07, 2.11, 2.14, 2.18, 2.22, 2.26, 2.30, 2.34, 2.38, 2.41, 2.45, 2.49, 2.53, 2.57, 2.61, 2.65, 2.69, 2.72, 2.76, 2.80, 2.84, 2.88, 2.92, 2.96, 2.99, 3.03, 3.07, 3.11, 3.15, 3.19, 3.23, 3.26, 3.30, 3.34, 3.38, 3.42, 3.46, 3.50, 3.53, 3.57, 3.61, 3.65, 3.69, 3.73, 3.77, 3.80, 3.84, 3.88, 3.92, 3.96, 4, 4.04, 4.07, 4.11, 4.15, 4.19, 4.23, 4.27, 4.31, 4.34, 4.38, 4.42, 4.46, 4.50, 4.54, 4.58, 4.61, 4.65, 4.69, 4.73, 4.77, 4.81, 4.85, 4.88, 4.92, 4.96, 5};
const float QUADRACHORUS[128] = {0.01, 0.01, 0.01, 0.01, 0.02, 0.02, 0.03, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.11, 0.12, 0.14, 0.15, 0.17, 0.19, 0.21, 0.23, 0.25, 0.27, 0.30, 0.32, 0.34, 0.37, 0.40, 0.43, 0.46, 0.49, 0.52, 0.55, 0.58, 0.62, 0.65, 0.69, 0.73, 0.76, 0.80, 0.84, 0.88, 0.93, 0.97, 1.01, 1.06, 1.10, 1.15, 1.20, 1.25, 1.3, 1.35, 1.4, 1.45, 1.51, 1.56, 1.62, 1.68, 1.73, 1.79, 1.85, 1.91, 1.98, 2.04, 2.10, 2.17, 2.23, 2.30, 2.37, 2.44, 2.51, 2.58, 2.65, 2.72, 2.80, 2.87, 2.95, 3.02, 3.10, 3.18, 3.26, 3.34, 3.42, 3.51, 3.59, 3.67, 3.76, 3.85, 3.93, 4.02, 4.11, 4.20, 4.29, 4.39, 4.48, 4.58, 4.67, 4.77, 4.87, 4.96, 5.06, 5.16, 5.27, 5.37, 5.47, 5.58, 5.68, 5.79, 5.90, 6.00, 6.11, 6.22, 6.34, 6.45, 6.56, 6.68, 6.79, 6.91, 7.03, 7.14, 7.26, 7.38, 7.50, 7.63, 7.75, 7.87, 8};
const float QUADRAECHOTIME[128] = {1, 1.12, 1.5, 2.12, 2.98, 4.1, 5.46, 7.07, 8.93, 11.04, 13.39, 16, 18.85, 21.95, 25.29, 28.89, 32.73, 36.83, 41.16, 45.74, 50.58, 55.66, 60.99, 66.56, 72.39, 78.46, 84.78, 91.35, 98.17, 105.23, 112.54, 120.10, 127.91, 135.97, 144.27, 152.82, 161.62, 170.67, 179.97, 189.51, 199.30, 209.34, 219.63, 230.16, 240.94, 251.97, 263.25, 274.78, 286.55, 298.58, 310.85, 323.36, 336.13, 349.14, 362.40, 375.91, 389.67, 403.68, 417.93, 432.43, 447.18, 462.17, 477.42, 492.91, 508.65, 524.64, 540.88, 557.36, 574.09, 591.07, 608.30, 625.77, 643.50, 661.47, 679.69, 698.15, 716.87, 735.83, 755.04, 774.50, 794.20, 814.16, 834.36, 854.81, 875.51, 896.45, 917.65, 939.09, 960.78, 982.71, 1004.90, 1027.33, 1050.01, 1072.94, 1096.12, 1119.54, 1143.21, 1167.13, 1191.30, 1215.72, 1240.38, 1265.29, 1290.45, 1315.86, 1341.52, 1367.42, 1393.57, 1419.97, 1446.62, 1473.51, 1500.65, 1528.04, 1555.68, 1583.57, 1611.7, 1640.08, 1668.71, 1697.59, 1726.72, 1756.09, 1785.71, 1815.58, 1845.7, 1876.06, 1906.67, 1937.54, 1968.64, 2000};
const float QUADRAARPSPEED[128] = {0.50, 0.5, 0.5, 0.51, 0.51, 0.52, 0.53, 0.54, 0.56, 0.57, 0.59, 0.61, 0.63, 0.65, 0.68, 0.70, 0.73, 0.76, 0.79, 0.82, 0.86, 0.9, 0.94, 0.98, 1.02, 1.06, 1.11, 1.16, 1.20, 1.26, 1.31, 1.36, 1.42, 1.48, 1.54, 1.6, 1.67, 1.73, 1.8, 1.87, 1.94, 2.01, 2.09, 2.16, 2.24, 2.32, 2.4, 2.49, 2.57, 2.66, 2.75, 2.84, 2.93, 3.03, 3.12, 3.22, 3.32, 3.42, 3.52, 3.63, 3.74, 3.85, 3.96, 4.07, 4.18, 4.3, 4.42, 4.54, 4.66, 4.78, 4.91, 5.03, 5.16, 5.29, 5.42, 5.56, 5.69, 5.83, 5.97, 6.11, 6.25, 6.40, 6.54, 6.69, 6.84, 7, 7.15, 7.3, 7.46, 7.62, 7.78, 7.94, 8.11, 8.28, 8.44, 8.61, 8.79, 8.96, 9.13, 9.31, 9.49, 9.67, 9.85, 10.04, 10.22, 10.41, 10.6, 10.79, 10.99, 11.18, 11.38, 11.58, 11.78, 11.98, 12.18, 12.39, 12.6, 12.81, 13.02, 13.23, 13.45, 13.66, 13.88, 14.10, 14.32, 14.55, 14.77, 15};
const float QUADRAVCFRES[128] = {10.0, 10.7, 11.4, 12.1, 12.8, 13.5, 14.3, 15, 15.7, 16.4, 17.1, 17.8, 18.5, 19.2, 19.9, 20.6, 21.3, 22, 22.8, 23.5, 24.2, 24.9, 25.6, 26.3, 27, 27.7, 28.4, 29.1, 29.8, 30.6, 31.3, 32, 32.7, 33.4, 34.1, 34.8, 35.5, 36.2, 36.9, 37.6, 38.3, 39.1, 39.8, 40.5, 41.2, 41.9, 42.6, 43.3, 44, 44.7, 45.4, 46.1, 46.9, 47.6, 48.3, 49, 49.7, 50.4, 51.1, 51.8, 52.5, 53.2, 53.9, 54.6, 55.4, 56.1, 56.8, 57.5, 58.2, 58.9, 59.6, 60.3, 61, 61.7, 62.4, 63.1, 63.9, 64.6, 65.3, 66, 66.7, 67.4, 68.1, 68.8, 69.5, 70.2, 70.9, 71.7, 72.4, 73.1, 73.8, 74.5, 75.2, 75.9, 76.6, 77.3, 78, 78.7, 79.4, 80.2, 80.9, 81.6, 82.3, 83, 83.7, 84.4, 85.1, 85.8, 86.5, 87.2, 88, 88.7, 89.4, 90.1, 90.8, 91.5, 92.2, 92.9, 93.6, 94.3, 95, 95.7, 96.5, 97.2, 97.9, 98.6, 99.3, 100};
const float QUADRAVOLUME[128] = {0.0, 0.0, 0.0, 0.1, 0.2, 0.3, 0.4, 0.6, 0.8, 1, 1.2, 1.5, 1.8, 2.1, 2.4, 2.8, 3.2, 3.6, 4, 4.5, 5, 5.5, 6, 6.6, 7.1, 7.8, 8.4, 9, 9.7, 10.4, 11.2, 11.9, 12.7, 13.5, 14.3, 15.2, 16.1, 17, 17.9, 18.9, 19.8, 20.8, 21.9, 22.9, 24, 25.1, 26.2, 27.4, 28.6, 29.8, 31, 32.3, 33.5, 34.8, 36.2, 37.5, 38.9, 40.3, 41.7, 43.2, 44.6, 46.1, 47.4, 49.2, 50.8, 52.4, 54, 55.7, 57.3, 59, 60.8, 62.5, 64.3, 66.1, 67.9, 69.8, 71.6, 73.5, 75.4, 77.4, 79.4, 81.4, 83.4, 85.4, 87.5, 89.6, 91.7, 93.9, 96, 98.2, 100.4, 102.7, 105, 107.2, 109.6, 111.9, 114.3, 116.7, 119.1, 121.5, 124, 126.5, 129, 131.6, 134.1, 136.7, 139.3, 142, 144.6, 147.3, 150, 152.8, 155.5, 158.3, 161.2, 164, 166.9, 169.7, 172.7, 175.6, 178.6, 181.5, 184.6, 187.6, 190.7, 193.8, 196.9, 200};
const float QUADRALFO[128] = {0.05, 0.06, 0.08, 0.1, 0.13, 0.17, 0.2, 0.24, 0.29, 0.33, 0.38, 0.43, 0.48, 0.54, 0.6, 0.65, 0.72, 0.78, 0.85, 0.91, 0.98, 1.05, 1.13, 1.2, 1.28, 1.35, 1.43, 1.51, 1.59, 1.68, 1.76, 1.85, 1.94, 2.03, 2.12, 2.21, 2.3, 2.4, 2.49, 2.59, 2.69, 2.79, 2.89, 2.99, 3.09, 3.20, 3.3, 3.41, 3.52, 3.63, 3.74, 3.85, 3.96, 4.08, 4.19, 4.31, 4.42, 4.54, 4.66, 4.78, 4.90, 5.02, 5.14, 5.27, 5.39, 5.52, 5.65, 5.77, 5.90, 6.03, 6.16, 6.29, 6.43, 6.56, 6.69, 6.83, 6.97, 7.1, 7.24, 7.38, 7.52, 7.66, 7.8, 7.94, 8.09, 8.23, 8.38, 8.52, 8.67, 8.82, 8.96, 9.11, 9.26, 9.41, 9.57, 9.72, 9.87, 10.03, 10.18, 10.34, 10.49, 10.65, 10.81, 10.97, 11.13, 11.29, 11.45, 11.61, 11.77, 11.93, 12.10, 12.26, 12.43, 12.6, 12.76, 12.93, 13.1, 13.27, 13.44, 13.61, 13.78, 13.95, 14.13, 14.30, 14.47, 14.65, 14.82, 15};

const String QUADRAECHOSYNC[20] = {"1/64 Triplet", "1/64", "1/64 Dotted", "1/32 Triplet", "1/32", "1/32 Dotted", "1/16 Triplet", "1/16", "1/16 Dotted", "1/8 Triplet", "1/8", "1/8 Dotted", "1/4 Triplet", "1/4", "1/4 Dotted", "1/2 Triplet", "1/2", "1/62 Dotted", "4 Beats", "8 Beats" };
const String QUADRAARPSYNC[20] = {"8 Beats", "4 Beats", "1/2 Dotted", "1/2", "1/2 Triplet", "1/4 Dotted", "1/4", "1/14 Triplet", "1/8 Dotted", "1/8", "1/8 Triplet", "1/16 Dotted", "1/16", "1/16 Triplet", "1/32 Dotted", "1/32", "1/32 Triplet", "1/64 Dotted", "1/64", "1/64 Triplet" };

#define RE_READ -9
#define  NO_OF_VOICES 1
#define NO_OF_PARAMS 270
const char* INITPATCHNAME = "Initial Patch";
#define HOLD_DURATION 1000
const uint32_t CLICK_DURATION = 250;
#define PATCHES_LIMIT 999
const String INITPATCH = "CC Mode,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1";
