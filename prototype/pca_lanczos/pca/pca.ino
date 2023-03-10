/**
* Demonstration of the eigenvalues / eigenvectors algorithm
* using PCA algorithm
*
* This example will load a dataset of Iris flower and perform
* a PCA on it to reduce the space while keeping the most important information
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lanczos.h"
#include "jacobi.h"

void readElement(void* element, const size_t sizeOfElement) {
  unsigned char* readElement = (unsigned char*)element;
  for (uint32_t i = 0; i < sizeOfElement; ++i) {
    while (Serial.available() < 1); // Wait for element
    Serial.readBytes(&readElement[i], sizeof(unsigned char));
  }
}

void writeElement(void* element, const size_t sizeOfElement) {
  while (Serial.availableForWrite() < sizeOfElement); // wait for write
  Serial.write((unsigned char*)element, sizeOfElement);
  Serial.flush(); // wait until data is sent
}


double findMean(uint8_t* ptr, uint16_t nbValues, uint16_t offset, uint8_t stride)
{
  double mean = 0;
  for(uint16_t i = 0; i < nbValues; ++i)
  {
    mean += ptr[i * stride + offset];
  }
  return mean / nbValues;
}


void computeCovarianceMatrix(uint8_t* ptr, uint8_t nbElemsCov, uint8_t nbElems, double* output)
{
  double means[nbElemsCov];
  for(uint8_t i = 0; i < nbElemsCov; ++i)
  {
    means[i] = findMean(ptr, nbElems, i, nbElemsCov);
  }

  for(uint8_t j = 0; j < nbElemsCov; ++j)
  {
    for(uint8_t k = 0; k < nbElemsCov; ++k)
    {
      double tmp = 0;
      for(uint16_t i = 0; i < nbElems; ++i)
      {
        tmp += ((ptr[i * nbElemsCov + j] - means[j]) * (ptr[i * nbElemsCov + k] - means[k]));
      }
      tmp /= (nbElems - 1);
      output[j * nbElemsCov + k] = tmp; 
    }
  }
}

/**
  Get the 2 highest indexes on the diagonal
  Values are returned in the returnArray
**/
void get2HighestIndexes(double* array, uint8_t arraySize, uint8_t* returnArray)
{
  double highestValue = 0.0, highestValue2 = 0.0;
  for(uint8_t i = 0; i < arraySize; ++i)
  {
    uint8_t index = i * arraySize + i;
    if(array[index] > highestValue)
    {
      highestValue2 = highestValue;
      returnArray[1] = returnArray[0];
      
      highestValue = array[index];
      returnArray[0] = i;
    }
    else if(array[index] > highestValue2)
    {
      highestValue2 = array[index];
      returnArray[1] = i;
    }
  } 
}

double applyTransformation(uint8_t* sample, double* wMatrix, uint8_t x, uint8_t y, uint8_t *size)
{
  double output = 0;
  for(uint8_t i = 0; i < size[2]; ++i)
  {
    output += sample[x * size[1] + i] * wMatrix[i * size[2] + y];
  }
  return output;
}

void setup()
{
  Serial.begin(9600);
}

void loop()
{ 
    //////////// Receive datasample from computer ///////////
    uint8_t nbValues;
    readElement(&nbValues, sizeof(nbValues));
    uint8_t nbCaracteristicPerEntries;
    readElement(&nbCaracteristicPerEntries, sizeof(nbCaracteristicPerEntries));

    uint8_t sample[nbValues * nbCaracteristicPerEntries];
    for(uint16_t i = 0; i < nbValues * nbCaracteristicPerEntries; ++i)
    {
      readElement(&sample[i], sizeof(uint8_t));
    }
    
    //////////// Compute covariance matrix ///////////
    // We do not normalize the data before computing the covariance matrix.
    // This is because all values are using the same scale and no normalization is needed
    // Normalizing the values would require to store them using float which would quadruple the memory usage
    // And we would need 1.6kB to store the sample rather than 400 bytes.
    double covarianceMatrix[nbCaracteristicPerEntries * nbCaracteristicPerEntries];
    computeCovarianceMatrix(sample, nbCaracteristicPerEntries, nbValues, covarianceMatrix);

    //////////// Call Lanczos to simplify eigen vectors/values calculation ///////////
    double tMatrix[nbCaracteristicPerEntries * nbCaracteristicPerEntries];
    memset(tMatrix, 0, nbCaracteristicPerEntries * nbCaracteristicPerEntries * sizeof(double));

    double vMatrix[nbCaracteristicPerEntries * nbCaracteristicPerEntries];
    lanczos(covarianceMatrix, nbCaracteristicPerEntries, nbCaracteristicPerEntries, NULL, tMatrix, vMatrix);
    
    //////////// Call jacobi to find eigenVectors and eigenValues ///////////
    double eigenVectors[nbCaracteristicPerEntries * nbCaracteristicPerEntries];
    jacobi(tMatrix, nbCaracteristicPerEntries, eigenVectors, 25, 1);

    //////////// Transform eigenVectors from lanczos dims to jacobi dims ///////////
    double realEigenVectors[nbCaracteristicPerEntries * nbCaracteristicPerEntries];
    uint_least8_t size[] = {nbCaracteristicPerEntries, nbCaracteristicPerEntries, nbCaracteristicPerEntries};
    matrixMultiply(vMatrix, eigenVectors, size, realEigenVectors);

    transpose(realEigenVectors, eigenVectors, size);

    uint8_t bestIndexes[2] = {0, 0};
    get2HighestIndexes(tMatrix, nbCaracteristicPerEntries, bestIndexes);

    //////////// Compute reduction matrix for PCA ///////////
    const uint8_t nbDimsAfterReduction = 2;
    double w_matrix[nbCaracteristicPerEntries * nbDimsAfterReduction];
    
    memcpy(w_matrix, &eigenVectors[nbCaracteristicPerEntries * bestIndexes[0]], nbCaracteristicPerEntries * sizeof(double));
    memcpy(&w_matrix[4], &eigenVectors[nbCaracteristicPerEntries * bestIndexes[1]], nbCaracteristicPerEntries * sizeof(double));

    double wTranspose[nbCaracteristicPerEntries * nbDimsAfterReduction];
    uint_least8_t size_2[] = {nbDimsAfterReduction, nbCaracteristicPerEntries};
    transpose(w_matrix, wTranspose, size_2);

    //////////// Apply PCA ///////////
    const uint8_t nbDimsAfterPCA = 2;
    writeElement(&nbDimsAfterPCA, sizeof(nbDimsAfterPCA));
    for(uint8_t i = 0; i < nbValues; ++i)
    {
      for(uint8_t j = 0; j < nbDimsAfterPCA; ++j)
      {
        uint8_t dims[] = {nbValues, nbCaracteristicPerEntries, nbDimsAfterPCA};
        float value = applyTransformation(sample, wTranspose, i, j, dims);
        writeElement(&value, sizeof(value));
      }
    }
}
