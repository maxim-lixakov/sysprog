//
// Created by Ликсаков Максим on 24.01.2024.
//

#include "merge_sort.h"
#include <stdio.h>


void merge(int arr[], int l, int m, int r){
    int i, j, k;
    int n1 = m -l + 1;
    int n2 = r -m;

    int L[n1], R[n2];

    // copy data to temp arrays:
    for (i=0; i < n1; i++){
        L[i] = arr[l+i];
    }
    for (j=0; j < n2; j++){
        R[j] = arr[m + 1 + j];
    }

    // now lets merge two temp arrays back but this time in sorted array
    i = 0;
    j = 0;
    k = l;

    while (i < n1 && j < n2){
        if (L[i] <= R[j]){
            arr[k] = L[i];
            i++;
        }
        else{
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    // now add elements that were left in R or L temp arrays and add them bubbled to our array

    while (i < n1){
        arr[i] = L[i];
        i++;
        k++;
    }

    while (j < n2){
        arr[k] = R[j];
        j++;
        k++;
    }
}

void mergeSort(int arr[], int l, int r){
    if (l < r){
        int m = l + (r - l) / 2;

        mergeSort(arr, l, m);
        mergeSort(arr, m + 1, r);

        merge(arr, l, m, r);
    }
}