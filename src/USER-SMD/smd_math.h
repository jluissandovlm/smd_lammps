/* ----------------------------------------------------------------------
 *
 *                    *** Smooth Mach Dynamics ***
 *
 * This file is part of the USER-SMD package for LAMMPS.
 * Copyright (2014) Georg C. Ganzenmueller, georg.ganzenmueller@emi.fhg.de
 * Fraunhofer Ernst-Mach Institute for High-Speed Dynamics, EMI,
 * Eckerstrasse 4, D-79104 Freiburg i.Br, Germany.
 *
 * ----------------------------------------------------------------------- */

//test
#ifndef SMD_MATH_H_
#define SMD_MATH_H_

namespace SMD_Math {
static inline void LimitDoubleMagnitude(double &x, const double limit) {
	/*
	 * if |x| exceeds limit, set x to limit with the sign of x
	 */
	if (fabs(x) > limit) { // limit delVdotDelR to a fraction of speed of sound
		x = limit * copysign(1, x);
	}
}

/*
 * deviator of a tensor
 */
static inline Matrix3d Deviator(const Matrix3d M) {
	Matrix3d eye;
	eye.setIdentity();
	eye *= M.trace() / 3.0;
	return M - eye;
}

/*
 * Polar Decomposition M = R * T
 * where R is a rotation and T a pure translation/stretch matrix.
 *
 * The decomposition is achieved using SVD, i.e. M = U S V^T,
 * where U = R V and S is diagonal.
 *
 *
 * For any physically admissible deformation gradient, the determinant of R must equal +1.
 * However, scenerios can arise, where the particles interpenetrate and cause inversion, leading to a determinant of R equal to -1.
 * In this case, the inversion direction is heuristically identified with the eigenvector of the smallest entry of S, which should work for most cases.
 * The sign of this corresponding eigenvalue is flipped, the original matrix M is recomputed using the flipped S, and the rotation and translation matrices are
 * obtained again from an SVD. The rotation should proper now, i.e., det(R) = +1.
 */

static inline bool PolDec(Matrix3d &M, Matrix3d &R, Matrix3d &T) {

	JacobiSVD < Matrix3d > svd(M, ComputeFullU | ComputeFullV); // SVD(A) = U S V*
	Vector3d S_eigenvalues = svd.singularValues();
	Matrix3d S = svd.singularValues().asDiagonal();
	Matrix3d U = svd.matrixU();
	Matrix3d V = svd.matrixV();
	Matrix3d eye;
	eye.setIdentity();

	// now do polar decomposition into M = R * T, where R is rotation
	// and T is translation matrix
	R = U * V.transpose();

	if (R.determinant() < 0.0) { // this is an improper rotation
		//printf("determinant of R=%f is not unity!\n", R->determinant());

		// identify the smallest entry in S and flip its sign
		int imin;
		S_eigenvalues.minCoeff(&imin);
		S(imin, imin) *= -1.0;

		Matrix3d F = U * S * V.transpose(); // recompute flipped deformation gradient
		svd.compute(F, ComputeFullU | ComputeFullV); // SVD(A) = U S V*
		U = svd.matrixU();
		V = svd.matrixV();
		R = U * V.transpose(); // extract proper rotation
				//printf("determinant of R after flipping is %f\", R->determinant());
	}

	/*
	 * scale S to avoid small principal strains
	 */

	double min = 0.3; // 0.3^2 = 0.09, should suffice for most problems
	double max = 2.0;
	for (int i = 0; i < 3; i++) {
		if (S(i, i) < min) {
			S(i, i) = min;
		} else if (S(i, i) > max) {
			S(i, i) = max;
		}
	}

	T = V * S * V.transpose();
	M = R * T;

	if (fabs(R.determinant() - 1.0) < 1.0e-8) {
		return true;
	} else {
		return false;
	}

//    cout << "Here is the difference between M and its reconstruction from the polar decomp:" << endl << Mdiff << endl;
//    cout << "Here is the Rotation matrix:" << endl << *R << endl;
//    cout << "Here is the Translation Matrix:" << endl << *T << endl;
//    cout << "is U unitary? Her is U^T * U:" << endl << R->transpose()* *R << endl;
//#endif
}

/*
 * Pseudo-inverse via SVD
 */

static inline Matrix3d pseudo_inverse_SVD(Matrix3d M) {

	JacobiSVD < Matrix3d > svd(M, ComputeFullU | ComputeFullV);

	Vector3d singularValuesInv;
	Vector3d singularValues = svd.singularValues();
	Matrix3d U = svd.matrixU();
	Matrix3d V = svd.matrixV();

//cout << "Here is the matrix V:" << endl << V * singularValues.asDiagonal() * U << endl;
//cout << "Its singular values are:" << endl << singularValues << endl;

	double pinvtoler = 1.0e-6;
	for (long row = 0; row < 3; row++) {
		if (singularValues(row) > pinvtoler) {
			singularValuesInv(row) = 1.0 / singularValues(row);
		} else {
			singularValuesInv(row) = 1.0;
		}
	}

	Matrix3d pInv;
	pInv = V * singularValuesInv.asDiagonal() * U.transpose();

	return pInv;
}

/*
 * test if two matrices are equal
 */
static inline double TestMatricesEqual(Matrix3d A, Matrix3d B, double eps) {
	Matrix3d diff;
	diff = A - B;
	double norm = diff.norm();
	if (norm > eps) {
		printf("Matrices A and B are not equal! The L2-norm difference is: %g\n", norm);
		cout << "Here is matrix A:" << endl << A << endl;
		cout << "Here is matrix B:" << endl << B << endl;
	}
	return norm;
}

/* ----------------------------------------------------------------------
 Limit eigenvalues of a matrix to upper and lower bounds.
 ------------------------------------------------------------------------- */

static inline Matrix3d LimitEigenvalues(Matrix3d S, double limitEigenvalue) {

	/*
	 * compute Eigenvalues of matrix S
	 */
	SelfAdjointEigenSolver < Matrix3d > es;
	es.compute(S);

	double max_eigenvalue = es.eigenvalues().maxCoeff();
	double min_eigenvalue = es.eigenvalues().minCoeff();
	double amax_eigenvalue = fabs(max_eigenvalue);
	double amin_eigenvalue = fabs(min_eigenvalue);

	if ((amax_eigenvalue > limitEigenvalue) || (amin_eigenvalue > limitEigenvalue)) {
		if (amax_eigenvalue > amin_eigenvalue) { // need to scale with max_eigenvalue
			double scale = amax_eigenvalue / limitEigenvalue;
			Matrix3d V = es.eigenvectors();
			Matrix3d S_diag = V.inverse() * S * V; // diagonalized input matrix
			S_diag /= scale;
			Matrix3d S_scaled = V * S_diag * V.inverse(); // undiagonalize matrix
			return S_scaled;
		} else { // need to scale using min_eigenvalue
			double scale = amin_eigenvalue / limitEigenvalue;
			Matrix3d V = es.eigenvectors();
			Matrix3d S_diag = V.inverse() * S * V; // diagonalized input matrix
			S_diag /= scale;
			Matrix3d S_scaled = V * S_diag * V.inverse(); // undiagonalize matrix
			return S_scaled;
		}
	} else { // limiting does not apply
		return S;
	}
}

static inline bool LimitMinMaxEigenvalues(Matrix3d &S, double min, double max) {

	/*
	 * compute Eigenvalues of matrix S
	 */
	SelfAdjointEigenSolver < Matrix3d > es;
	es.compute(S);

	if ((es.eigenvalues().maxCoeff() > max) || (es.eigenvalues().minCoeff() < min)) {
		Matrix3d S_diag = es.eigenvalues().asDiagonal();
		Matrix3d V = es.eigenvectors();
		for (int i = 0; i < 3; i++) {
			if (S_diag(i, i) < min) {
				//printf("limiting eigenvalue %f --> %f\n", S_diag(i, i), min);
				//printf("these are the eigenvalues of U: %f %f %f\n", es.eigenvalues()(0), es.eigenvalues()(1), es.eigenvalues()(2));
				S_diag(i, i) = min;
			} else if (S_diag(i, i) > max) {
				//printf("limiting eigenvalue %f --> %f\n", S_diag(i, i), max);
				S_diag(i, i) = max;
			}
		}
		S = V * S_diag * V.inverse(); // undiagonalize matrix
		return true;
	} else {
		return false;
	}
}

}

#endif /* SMD_MATH_H_ */
