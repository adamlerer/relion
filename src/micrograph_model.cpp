/***************************************************************************
 *
 * Author: "Sjors H.W. Scheres", "Takanori Nakane"
 * MRC Laboratory of Molecular Biology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/

#include "src/micrograph_model.h"
#include "src/metadata_table.h"
#include "src/image.h"

const RFLOAT Micrograph::NOT_OBSERVED = -9999;
const int ThirdOrderPolynomialModel::NUM_COEFFS_PER_DIM = 18;

int ThirdOrderPolynomialModel::getShiftAt(RFLOAT z, RFLOAT x, RFLOAT y, RFLOAT &shiftx, RFLOAT &shifty) const {
	const RFLOAT x2 = x * x, y2 = y * y, xy = x * y, z2 = z * z;
	const RFLOAT z3 = z2 * z;

	shiftx = (coeffX(0)  * z + coeffX(1)  * z2 + coeffX(2)  * z3) \
	         + (coeffX(3)  * z + coeffX(4)  * z2 + coeffX(5)  * z3) * x \
	         + (coeffX(6)  * z + coeffX(7)  * z2 + coeffX(8)  * z3) * x2 \
	         + (coeffX(9)  * z + coeffX(10) * z2 + coeffX(11) * z3) * y \
	         + (coeffX(12) * z + coeffX(13) * z2 + coeffX(14) * z3) * y2 \
	         + (coeffX(15) * z + coeffX(16) * z2 + coeffX(17) * z3) * xy;
	shifty = (coeffY(0)  * z + coeffY(1)  * z2 + coeffY(2)  * z3)\
	         + (coeffY(3)  * z + coeffY(4)  * z2 + coeffY(5)  * z3) * x \
	         + (coeffY(6)  * z + coeffY(7)  * z2 + coeffY(8)  * z3) * x2 \
	       	 + (coeffY(9)  * z + coeffY(10) * z2 + coeffY(11) * z3) * y \
	         + (coeffY(12) * z + coeffY(13) * z2 + coeffY(14) * z3) * y2 \
	         + (coeffY(15) * z + coeffY(16) * z2 + coeffY(17) * z3) * xy;
}

void ThirdOrderPolynomialModel::write(std::ostream &fh, std::string block_name) {
	MetaDataTable MD;
	MD.setName(block_name);

	int coeff_idx = 0;

	// Write coeffX
	for (int i = 0; i < NUM_COEFFS_PER_DIM; i++) {
		MD.addObject();
                MD.setValue(EMDL_MICROGRAPH_MOTION_COEFFS_IDX, coeff_idx);
                MD.setValue(EMDL_MICROGRAPH_MOTION_COEFF, coeffX(i));
		coeff_idx++;
	}

	// Write coeffY	
	for (int i = 0; i < NUM_COEFFS_PER_DIM; i++) {
		MD.addObject();
                MD.setValue(EMDL_MICROGRAPH_MOTION_COEFFS_IDX, coeff_idx);
                MD.setValue(EMDL_MICROGRAPH_MOTION_COEFF, coeffY(i));
		coeff_idx++;
	}

	MD.write(fh);
}

void ThirdOrderPolynomialModel::read(std::ifstream &fh, std::string block_name) {
	MetaDataTable MD;
	MD.readStar(fh, block_name);

	const int NUM_COEFFS = NUM_COEFFS_PER_DIM * 2;
	int num_read = 0;

	coeffX.resize(NUM_COEFFS_PER_DIM); coeffX.initZeros();
	coeffY.resize(NUM_COEFFS_PER_DIM); coeffY.initZeros();

	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD) {
		int idx;
		RFLOAT val;

		if (!MD.getValue(EMDL_MICROGRAPH_MOTION_COEFFS_IDX, idx) ||
		    !MD.getValue(EMDL_MICROGRAPH_MOTION_COEFF, val)) {
			REPORT_ERROR("ThirdOrderPolynomialModel coefficients table: missing index or coefficients");
		}

		if (idx >= 0 && idx < NUM_COEFFS_PER_DIM) {
			coeffX(idx) = val;
		} else if (idx >= NUM_COEFFS_PER_DIM && idx < NUM_COEFFS) {
			coeffY(idx) = val;
		} else {
			REPORT_ERROR("ThirdOrderPolynomialModel coefficients table: wrong index");
		}
		num_read++;
	}
	
	if (num_read != NUM_COEFFS) {
		REPORT_ERROR("ThirdOrderPolynomialModel coefficients table: incomplete values");
	}
}

void Micrograph::setMovie(FileName fnMovie, FileName fnGain, RFLOAT binning) {
	Image<RFLOAT> Ihead;
	Ihead.read(fnMovie, false);
        
	width = XSIZE(Ihead());
	height = YSIZE(Ihead());
	n_frames = NSIZE(Ihead());

	this->binning = binning;

	globalShiftX.resize(n_frames, NOT_OBSERVED);
	globalShiftY.resize(n_frames, NOT_OBSERVED);

	this->fnMovie = fnMovie;
	this->fnGain = fnGain;
}

// Read from a STAR file
void Micrograph::read(FileName fn_in)
{
	// Clear current model
	clear();

	// Open input file
	std::ifstream in(fn_in.data(), std::ios_base::in);
	if (in.fail()) {
		REPORT_ERROR( (std::string) "MicrographModel::read: File " + fn_in + " cannot be read." );
	}

	MetaDataTable MDglobal;

	// Read Image metadata
	MDglobal.readStar(in, "general");

	if (!MDglobal.getValue(EMDL_IMAGE_SIZEX, width) ||
	    !MDglobal.getValue(EMDL_IMAGE_SIZEY, height) ||
	    !MDglobal.getValue(EMDL_IMAGE_SIZEZ, n_frames) ||
	    !MDglobal.getValue(EMDL_MICROGRAPH_MOVIE_NAME, fnMovie)) {
		REPORT_ERROR("MicrographModel::read: insufficient general information");
	}

	globalShiftX.resize(n_frames, NOT_OBSERVED);
	globalShiftY.resize(n_frames, NOT_OBSERVED);

	if (!MDglobal.getValue(EMDL_MICROGRAPH_GAIN_NAME, fnGain)) {
		fnGain = "";
	}
	if (!MDglobal.getValue(EMDL_MICROGRAPH_BINNING, binning)) {
		binning = 1.0;
	}
	if (!MDglobal.getValue(EMDL_MICROGRAPH_ORIGINAL_PIXEL_SIZE, angpix)) {
		angpix = -1;
	}
	if (!MDglobal.getValue(EMDL_MICROGRAPH_PRE_EXPOSURE, pre_exposure)) {
		pre_exposure = -1;
	}
	if (!MDglobal.getValue(EMDL_MICROGRAPH_DOSE_RATE, dose_per_frame)) {
		dose_per_frame = -1;
	}
	if (!MDglobal.getValue(EMDL_CTF_VOLTAGE, voltage)) {
		voltage = -1;
	}
	int model_version;
	if (!MDglobal.getValue(EMDL_MICROGRAPH_MOTION_MODEL_VERSION, model_version)) {
		if (model_version == MOTION_MODEL_THIRD_ORDER_POLYNOMIAL) {
			model = new ThirdOrderPolynomialModel();
		} else if (model_version == MOTION_MODEL_NULL) {
			model = NULL;
		} else {
			std::cerr << "Warning: Ignoring unknown motion model " << model_version << std::endl;
		}
		model->read(in, "local_motion_model");
	} else {
		model = NULL;
	}

	// Read global shifts
	int frame;
	RFLOAT shiftX, shiftY;

	MDglobal.readStar(in, "global_shift");

	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MDglobal)
	{
		if (!MDglobal.getValue(EMDL_MICROGRAPH_FRAME_NUMBER, frame) ||
		    !MDglobal.getValue(EMDL_MICROGRAPH_SHIFT_X, shiftX) ||
		    !MDglobal.getValue(EMDL_MICROGRAPH_SHIFT_Y, shiftY)) { 
			REPORT_ERROR("MicrographModel::read: incorrect global_shift table");
		}

		// frame is 1-indexed!
		globalShiftX[frame - 1] = shiftX;
		globalShiftY[frame - 1] = shiftY;
		std::cout << " global shift: frame #" << frame << " x " << shiftX << " Y " << shiftY << std::endl;
	}
}

// Write to a STAR file
void Micrograph::write(FileName filename) {
	std::ofstream fh;
	MetaDataTable MD;

	fh.open(filename.c_str());
	if (!fh) {
		REPORT_ERROR((std::string)"Micrograph::write: Cannot write file: " + filename);
	}

        MD.setName("general");
        MD.setIsList(true);
        MD.addObject();
	MD.setValue(EMDL_IMAGE_SIZEX, width);
        MD.setValue(EMDL_IMAGE_SIZEY, height);
        MD.setValue(EMDL_IMAGE_SIZEZ, n_frames);
        MD.setValue(EMDL_MICROGRAPH_MOVIE_NAME, fnMovie);
	if (fnGain != "") {
		MD.setValue(EMDL_MICROGRAPH_GAIN_NAME, fnGain);
	}
	MD.setValue(EMDL_MICROGRAPH_BINNING, binning);
	if (angpix != -1) {
		MD.setValue(EMDL_MICROGRAPH_ORIGINAL_PIXEL_SIZE, angpix);
        }
	if (dose_per_frame != -1) {
		MD.setValue(EMDL_MICROGRAPH_DOSE_RATE, dose_per_frame);
        }
	if (pre_exposure != -1) {
		MD.setValue(EMDL_MICROGRAPH_PRE_EXPOSURE, pre_exposure);
        }
	if (voltage != -1) {
		MD.setValue(EMDL_CTF_VOLTAGE, voltage);
        }
	if (model != NULL) {
		MD.setValue(EMDL_MICROGRAPH_MOTION_MODEL_VERSION, model->getModelVersion());
	} else {
		MD.setValue(EMDL_MICROGRAPH_MOTION_MODEL_VERSION, MOTION_MODEL_NULL);
	}
	MD.write(fh);

	MD.clear();
	MD.setName("global_shift");
	for (int frame = 0; frame < n_frames; frame++) {
		MD.addObject();
		MD.setValue(EMDL_MICROGRAPH_FRAME_NUMBER, frame + 1); // make 1-indexed
		MD.setValue(EMDL_MICROGRAPH_SHIFT_X, globalShiftX[frame]);
		MD.setValue(EMDL_MICROGRAPH_SHIFT_Y, globalShiftY[frame]);
	}
	MD.write(fh);

	if (model != NULL) {
		std::string block_name = "local_motion_model";
		model->write(fh, block_name);
	}

	fh.close();
}

int Micrograph::getShiftAt(RFLOAT frame, RFLOAT x, RFLOAT y, RFLOAT &shiftx, RFLOAT &shifty) const {
	if (model != NULL) {
		model->getShiftAt(frame, x, y, shiftx, shifty);
	} else {
		shiftx = 0;
		shifty = 0;
	}

	// frame is 1-indexed!
	shiftx += globalShiftX[frame - 1];
	shifty += globalShiftY[frame - 1];
}

void Micrograph::setGlobalShift(int frame, RFLOAT shiftx, RFLOAT shifty) {
	if (frame <= 0 || frame > n_frames) {
		std::cout << "Frame: " << frame << " n_frames: " << n_frames << std::endl;
		REPORT_ERROR("Micrograph::setGlobalShift() frame out of range");
	}

	frame--;
	globalShiftX[frame] = shiftx;
	globalShiftY[frame] = shifty;
}