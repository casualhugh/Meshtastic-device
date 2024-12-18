#include "LSM303.h"
void LSM303::transformation(float uncalibrated_values[3], float* calibrated_values)    
{
  //calibration_matrix[3][3] is the transformation matrix
  //replace M11, M12,..,M33 with your transformation matrix data
  // double calibration_matrix[3][3] = 
  // {
  //   {1.328, 0.068, 0.017},
  //   {0.078, 1.302, -0.195},
  //   {0.048, 0.004, 1.708}  
  // };
  //bias[3] is the bias
  //replace Bx, By, Bz with your bias data
  // double bias[3] = 
  // {
  //   -51.646,
  //   637.377,
  //   306.668
  // }; 
  //calculation
  for (int i=0; i<3; ++i){
    uncalibrated_values[i] = uncalibrated_values[i] - bias[i];
  }
  float result[3] = {0, 0, 0};
  for (int i=0; i<3; ++i)
    for (int j=0; j<3; ++j)
      result[i] += calibration_matrix[i][j] * uncalibrated_values[j];
  for (int i=0; i<3; ++i) calibrated_values[i] = result[i];
}

void LSM303::init(){
    // acc->begin();
    mag->begin();
    for (int i = 0; i < 10; i++){
      last_values[i] = 0;
    }
}

bool LSM303::isEnabled(){
    return enabled;
}

void LSM303::enable(){
    mag->begin();
    //acc->Enable();
    mag->Enable();
    enabled = true;
}

void LSM303::turn_off(){
    disable();
    // acc->Disable();
    mag->Disable();
    enabled = false;
}
void LSM303::recalculate_bias(){
  for (int i = 0; i < 3; i++){
    if (mins[i] < maxes[i]){
      bias[i] = ((double)maxes[i] + (double)mins[i]) / 2.0;
    }
  }
}


float LSM303::getAverage(float last){
  // Count the number of values between 90 and 270
  int count_mid = 0;
  int count_zeros = 0;
  if (90 < last && last < 270){
    count_mid += 1;
  }
  // Shift all the values up by 1
  for (int i = 9; i >= 0; i--){
    last_values[i+1] = last_values[i];
    if (90 < last_values[i] && last_values[i] < 270){
      count_mid += 1;
    }
    if (last_values[i] == 0){
      count_zeros += 1;
    }
  }
  last_values[0] = last;
  if (count_zeros > 7){
    // still filling the defaults
    return last;
  }
  
  float sum = 0;
  // We are likely in the 90 to 270 range so we can just take the average
  if (count_mid > 5){
    for (int i = 0; i < 10; i++){
      sum += last_values[i];
    }
  } else {
    // We might be between 270 and 90 we want to shift all the values between 90 and 270 to take the average 
    // Then switch it back in the return
    for (int i = 0; i < 10; i++){
      if (last_values[i] > 270){
        sum += last_values[i] - 180;
      } else {
        sum += last_values[i] + 180;
      }
    }
  }
  float average = sum / 10;
  if (count_mid <= 5){
    average += 90;
    if (average > 360){
      average -= 360;
    }
  }
  
  return average;

}
/*
North = 0/360
East = 90
South = 180
West = 270
*/
float LSM303::getHeading()
{
    int32_t magnetometer[3];
    mag->GetAxes(magnetometer);
    count+= 1;
    float data[3];
    float mag[3];
    for (int i = 0; i < 3; i++){
        mag[i] = (float)magnetometer[i];
        if (magnetometer[i] > maxes[i]){
          maxes[i] = magnetometer[i];
        }
        if (magnetometer[i] < mins[i]){
          mins[i] = magnetometer[i];
        }
    }
    if (count > 20){
        recalculate_bias();
        count = 0;
    }
    transformation(mag, data);


    float deg = atan2(data[1], data[0]) * RAD_TO_DEG;

    float declination = 12.75;
    // Byron float declination = 11.62;
    deg += declination;
    deg += 90;
    while (deg < 0){
      deg += 360;
    }
    while (deg > 360){
      deg -= 360;
    }

    return round(deg);
}