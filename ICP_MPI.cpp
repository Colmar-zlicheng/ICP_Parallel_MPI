#include <iostream>
#include <iomanip> // use setw() in cout
#include <string>
#include <time.h>
#include "dataloader.h"
#include "Jacobi.h"
#include <mpi.h>
using namespace std;

PointXYZ transform_matrix(PointXYZ before, double R[3][3], double t[3])
{
     PointXYZ after;
     after.x = R[0][0] * before.x + R[0][1] * before.y + R[0][2] * before.z + t[0];
     after.y = R[1][0] * before.x + R[1][1] * before.y + R[1][2] * before.z + t[1];
     after.z = R[2][0] * before.x + R[2][1] * before.y + R[2][2] * before.z + t[2];
     return after;
}

double compute_error(PointXYZ center_before, PointXYZ center_after,
                     vector<PointXYZ> points_before, vector<PointXYZ> points_after,
                     double R[3][3], double t[3])
{
     double error = 0, e_x = 0, e_y = 0, e_z = 0, error_p = 0, e_x_p = 0, e_y_p = 0, e_z_p = 0;
     int num_points = points_after.size();
     PointXYZ tmp_before, tmp_after;
     for (int i = 0; i < num_points; i++)
     {
          tmp_before.x = points_before[i].x + center_before.x;
          tmp_before.y = points_before[i].y + center_before.y;
          tmp_before.z = points_before[i].z + center_before.z;
          tmp_after.x = points_after[i].x + center_after.x;
          tmp_after.y = points_after[i].y + center_after.y;
          tmp_after.z = points_after[i].z + center_after.z;

          PointXYZ transformed = transform_matrix(tmp_before, R, t);
          e_x += (tmp_after.x - transformed.x);
          e_y += (tmp_after.y - transformed.y);
          e_z += (tmp_after.z - transformed.z);
          e_x_p += abs(tmp_after.x - transformed.x);
          e_y_p += abs(tmp_after.y - transformed.y);
          e_z_p += abs(tmp_after.z - transformed.z);
     }

     e_x = e_x / center_before.x;
     e_y = e_y / center_before.y;
     e_z = e_z / center_before.z;
     e_x_p = e_x_p / (double(num_points) * center_before.x);
     e_y_p = e_y_p / (double(num_points) * center_before.y);
     e_z_p = e_z_p / (double(num_points) * center_before.z);
     error = (e_x + e_y + e_z) / 3;
     error_p = (e_x_p + e_y_p + e_z_p) / 3;
     cout << "Global Error: " << error * 100 << "%" << endl;
     cout << "Point average Error: " << error_p * 100 << "%" << endl;
     return error;
}

int main(int argc, char **argv)
{
     // get start time
     clock_t start_time = clock();

     string data_before, data_after;
     data_before = "data/points_before.pcd";
     data_after = "data/points_after.pcd"; // 549537 points

     PointXYZ center_before;
     PointXYZ center_after;
     vector<PointXYZ> points_before;
     vector<PointXYZ> points_after;
     bool Decentroided = true; // Compute Decentroided Point Clouds
     // read points from pcd file and pretreat data
     center_before = readPCD(points_before, data_before, 11, Decentroided);
     center_after = readPCD(points_after, data_after, 13, Decentroided);

     // 初始化 MPI
     MPI_Init(&argc, &argv);
     int rank, size;
     MPI_Comm_rank(MPI_COMM_WORLD, &rank);
     MPI_Comm_size(MPI_COMM_WORLD, &size);

     // 时间
     clock_t mid_time = clock();

     // 分块预处理
     int num_points;
     num_points = points_after.size();
     int chunk_size = num_points / size;
     int start = rank * chunk_size;
     int end = (rank + 1) * chunk_size;

     // 将点云数据分块分发给所有进程
     vector<PointXYZ> chunk_points_before(points_before.begin() + start, points_before.begin() + end);
     vector<PointXYZ> chunk_points_after(points_after.begin() + start, points_after.begin() + end);

     // 构造H
     double H[3][3] = {0};

     // 分别进行矩阵运算

     for (int i = 0; i < chunk_size; i++)
     {
          H[0][0] += chunk_points_after[i].x * chunk_points_before[i].x;
          H[1][0] += chunk_points_after[i].y * chunk_points_before[i].x;
          H[2][0] += chunk_points_after[i].z * chunk_points_before[i].x;
          H[0][1] += chunk_points_after[i].x * chunk_points_before[i].y;
          H[1][1] += chunk_points_after[i].y * chunk_points_before[i].y;
          H[2][1] += chunk_points_after[i].z * chunk_points_before[i].y;
          H[0][2] += chunk_points_after[i].x * chunk_points_before[i].z;
          H[1][2] += chunk_points_after[i].y * chunk_points_before[i].z;
          H[2][2] += chunk_points_after[i].z * chunk_points_before[i].z;
     }

     double result[size][3][3] = {0};
     // 进程0对H进行汇总加和
     MPI_Gather(H, 1 * 3 * 3, MPI_DOUBLE, result, 3 * 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);

     if (rank == 0)
     {
          for (int i = 1; i < size; i++)
          {
               result[0][0][0] += result[i][0][0];
               result[0][1][0] += result[i][1][0];
               result[0][2][0] += result[i][2][0];
               result[0][0][1] += result[i][0][1];
               result[0][1][1] += result[i][1][1];
               result[0][2][1] += result[i][2][1];
               result[0][0][2] += result[i][0][2];
               result[0][1][2] += result[i][1][2];
               result[0][2][2] += result[i][2][2];
          }

          H[0][0] = result[0][0][0] / double(num_points);
          H[1][0] = result[0][1][0] / double(num_points);
          H[2][0] = result[0][2][0] / double(num_points);
          H[0][1] = result[0][0][1] / double(num_points);
          H[1][1] = result[0][1][1] / double(num_points);
          H[2][1] = result[0][2][1] / double(num_points);
          H[0][2] = result[0][0][2] / double(num_points);
          H[1][2] = result[0][1][2] / double(num_points);
          H[2][2] = result[0][2][2] / double(num_points);

          double AAT[3][3] = {0}, ATA[3][3] = {0};
          AAT[0][0] = H[0][0] * H[0][0] + H[0][1] * H[0][1] + H[0][2] * H[0][2];
          AAT[1][0] = H[1][0] * H[0][0] + H[1][1] * H[0][1] + H[1][2] * H[0][2];
          AAT[2][0] = H[2][0] * H[0][0] + H[2][1] * H[0][1] + H[2][2] * H[0][2];
          AAT[0][1] = H[0][0] * H[1][0] + H[0][1] * H[1][1] + H[0][2] * H[1][2];
          AAT[1][1] = H[1][0] * H[1][0] + H[1][1] * H[1][1] + H[1][2] * H[1][2];
          AAT[2][1] = H[2][0] * H[1][0] + H[2][1] * H[1][1] + H[2][2] * H[1][2];
          AAT[0][2] = H[0][0] * H[2][0] + H[0][1] * H[2][1] + H[0][2] * H[2][2];
          AAT[1][2] = H[1][0] * H[2][0] + H[1][1] * H[2][1] + H[1][2] * H[2][2];
          AAT[2][2] = H[2][0] * H[2][0] + H[2][1] * H[2][1] + H[2][2] * H[2][2];

          ATA[0][0] = H[0][0] * H[0][0] + H[1][0] * H[1][0] + H[2][0] * H[2][0];
          ATA[1][0] = H[0][1] * H[0][0] + H[1][1] * H[1][0] + H[2][1] * H[2][0];
          ATA[2][0] = H[0][2] * H[0][0] + H[1][2] * H[1][0] + H[2][2] * H[2][0];
          ATA[0][1] = H[0][0] * H[0][1] + H[1][0] * H[1][1] + H[2][0] * H[2][1];
          ATA[1][1] = H[0][1] * H[0][1] + H[1][1] * H[1][1] + H[2][1] * H[2][1];
          ATA[2][1] = H[0][2] * H[0][1] + H[1][2] * H[1][1] + H[2][2] * H[2][1];
          ATA[0][1] = H[0][0] * H[0][2] + H[1][0] * H[1][1] + H[2][0] * H[2][2];
          ATA[1][1] = H[0][1] * H[0][2] + H[1][1] * H[1][1] + H[2][1] * H[2][2];
          ATA[2][1] = H[0][2] * H[0][2] + H[1][2] * H[1][1] + H[2][2] * H[2][2];

          int n = 3;
          double eps = 1e-10;
          int iter = 1000;
          double U_eigenvalues[3] = {0};
          double V_eigenvalues[3] = {0};
          double U[3][3] = {0}, V[3][3] = {0}; // eigenvectors
          bool u = Jacobi(&AAT[0][0], n, &U[0][0], U_eigenvalues, eps, iter);
          bool tt = Jacobi(&ATA[0][0], n, &V[0][0], V_eigenvalues, eps, iter);

          // cout << "U: " << endl;
          // cout << "  " << U[0][0] << " " << U[0][1] << " " << U[0][2] << endl
          //      << "  " << U[1][0] << " " << U[1][1] << " " << U[1][2] << endl
          //      << "  " << U[2][0] << " " << U[2][1] << " " << U[2][2] << endl;
          // cout << "V: " << endl;
          // cout << "  " << V[0][0] << " " << V[0][1] << " " << V[0][2] << endl
          //      << "  " << V[1][0] << " " << V[1][1] << " " << V[1][2] << endl
          //      << "  " << V[2][0] << " " << V[2][1] << " " << V[2][2] << endl;

          // compute R, $ R = U * V^T $
          double R[3][3] = {0};
          R[0][0] = U[0][0] * V[0][0] + U[0][1] * V[0][1] + U[0][2] * V[0][2];
          R[1][0] = U[1][0] * V[0][0] + U[1][1] * V[0][1] + U[1][2] * V[0][2];
          R[2][0] = U[2][0] * V[0][0] + U[2][1] * V[0][1] + U[2][2] * V[0][2];
          R[0][1] = U[0][0] * V[1][0] + U[0][1] * V[1][1] + U[0][2] * V[1][2];
          R[1][1] = U[1][0] * V[1][0] + U[1][1] * V[1][1] + U[1][2] * V[1][2];
          R[2][1] = U[2][0] * V[1][0] + U[2][1] * V[1][1] + U[2][2] * V[1][2];
          R[0][2] = U[0][0] * V[2][0] + U[0][1] * V[2][1] + U[0][2] * V[2][2];
          R[1][2] = U[1][0] * V[2][0] + U[1][1] * V[2][1] + U[1][2] * V[2][2];
          R[2][2] = U[2][0] * V[2][0] + U[2][1] * V[2][1] + U[2][2] * V[2][2];

          cout << "R: " << endl;
          for (int i = 0; i < n; i++)
          {
               for (int j = 0; j < n; j++)
                    cout << setw(12) << R[i][j];
               cout << endl;
          }

          // compute t, $ t = X_u - R * Y_u $ (X=RY+t)
          // X_u = center_after, Y_u = center_before
          double t[3] = {0};
          t[0] = center_after.x - R[0][0] * center_before.x - R[0][1] * center_before.y - R[0][2] * center_before.z;
          t[1] = center_after.y - R[1][0] * center_before.x - R[1][1] * center_before.y - R[1][2] * center_before.z;
          t[2] = center_after.z - R[2][0] * center_before.x - R[2][1] * center_before.y - R[2][2] * center_before.z;

          cout << "t: " << endl;
          for (int i = 0; i < n; i++)
               cout << setw(10) << t[i] << endl;
          cout << endl;

          // compute running time
          clock_t end_time = clock();
          cout << "Dataloader Running Time(MPI): " << static_cast<double>(mid_time - start_time) / CLOCKS_PER_SEC << "s" << endl;
          cout << "ICP Running Time(MPI): " << static_cast<double>(end_time - mid_time) / CLOCKS_PER_SEC << "s" << endl;
          cout << "Total Running Time(MPI): " << static_cast<double>(end_time - start_time) / CLOCKS_PER_SEC << "s" << endl;

          // 计算并行化花费的时间
          double error = compute_error(center_before, center_after, points_before, points_after, R, t);

     } //(进程0结束)
     MPI_Finalize();

     // compute error

     return 0;
}
